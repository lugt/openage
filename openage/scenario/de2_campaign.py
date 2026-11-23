# Copyright 2026 the openage authors. See copying.md for legal info.
"""Read local DE2 campaign containers, including the macOS GPV wrapper.

The cipher is AES-256-CTR with the installation's substitution table. Format
reference: https://github.com/Niedzielan/aoe2_gpv_decrypt_py . No game keys are
stored here. The verified macOS layout reads parameters from the user's binary.
Campaign table reference: https://github.com/withmorten/rge_campaign .
"""

from pathlib import Path
import struct


def _expand_key(key, sbox):
    words = [list(key[i:i + 4]) for i in range(0, 32, 4)]
    rcon = 1
    for index in range(8, 60):
        previous = words[-1].copy()
        if index % 8 == 0:
            previous = [sbox[b] for b in previous[1:] + previous[:1]]
            previous[0] ^= rcon
            rcon = ((rcon << 1) ^ (0x11b if rcon & 128 else 0)) & 255
        elif index % 8 == 4:
            previous = [sbox[b] for b in previous]
        words.append([a ^ b for a, b in zip(words[index - 8], previous)])
    return [b for word in words for b in word]


def _cipher_block(block, expanded, sbox):
    state = [a ^ b for a, b in zip(block, expanded[:16])]
    for round_index in range(1, 15):
        state = [sbox[state[4 * ((column + row) % 4) + row]]
                 for column in range(4) for row in range(4)]
        if round_index != 14:
            for column in range(0, 16, 4):
                values = state[column:column + 4]
                total = values[0] ^ values[1] ^ values[2] ^ values[3]
                for row in range(4):
                    delta = values[row] ^ values[(row + 1) % 4]
                    doubled = ((delta << 1) ^ (0x11b if delta & 128 else 0)) & 255
                    state[column + row] = values[row] ^ total ^ doubled
        state = [a ^ b for a, b in zip(state, expanded[round_index * 16:round_index * 16 + 16])]
    return bytes(state)


def _local_parameters(executable):
    """Parameters of the retained Feral macOS ARM64 build, validated on use."""
    with Path(executable).open('rb') as source:
        if source.read(8) != bytes.fromhex('cffaedfe0c000001'):
            raise ValueError('This GPV reader requires the local macOS ARM64 executable or an unpacked campaign')
        source.seek(0x46b3d98)
        key = source.read(32)
        source.seek(0x46b3c90)
        iv = source.read(16)
        source.seek(0x4610810)
        sbox = source.read(256)
    if len(key) != 32 or len(iv) != 16 or len(set(sbox)) != 256:
        raise ValueError('Unsupported local game executable layout')
    return key, iv, sbox


def unpack_campaign(source, destination, executable=None):
    """Copy/decrypt a campaign in bounded chunks without modifying its source."""
    source, destination = Path(source), Path(destination)
    temporary = destination.with_suffix(destination.suffix + '.tmp')
    destination.parent.mkdir(parents=True, exist_ok=True)
    with source.open('rb') as input_file:
        magic = input_file.read(4)
        if magic == b'2.00':
            input_file.seek(0)
            with temporary.open('wb') as output:
                while chunk := input_file.read(65536):
                    output.write(chunk)
        else:
            if magic != b'esaB' or executable is None:
                raise ValueError('Expected the original Base GPV campaign and local game executable')
            size_bytes = input_file.read(8)
            if len(size_bytes) != 8 or struct.unpack('<Q', size_bytes)[0] != source.stat().st_size - 12:
                raise ValueError('Invalid GPV payload length')
            key, iv, sbox = _local_parameters(executable)
            expanded = _expand_key(key, sbox)
            counter = int.from_bytes(iv, 'big')
            first = input_file.read(16)
            preview = bytes(a ^ b for a, b in zip(first, _cipher_block(iv, expanded, sbox)))
            if not preview.startswith(b'2.00'):
                raise ValueError('Local executable parameters do not match this GPV campaign')
            input_file.seek(12)
            with temporary.open('wb') as output:
                while chunk := input_file.read(65536):
                    decoded = bytearray()
                    for index in range(0, len(chunk), 16):
                        stream = _cipher_block(counter.to_bytes(16, 'big'), expanded, sbox)
                        decoded.extend(a ^ b for a, b in zip(chunk[index:index + 16], stream))
                        counter = (counter + 1) % (1 << 128)
                    output.write(decoded)
    temporary.replace(destination)
    return destination


def campaign_entries(path):
    """Read the v2 table, checking bounds before any scenario is extracted."""
    path = Path(path)
    file_size = path.stat().st_size
    with path.open('rb') as source:
        def read(size):
            data = source.read(size)
            if len(data) != size:
                raise ValueError('Truncated campaign table')
            return data

        def integer():
            return struct.unpack('<I', read(4))[0]

        def string():
            marker, length = struct.unpack('<HH', read(4))
            if marker != 0x0a60:
                raise ValueError('Invalid campaign string marker')
            return read(length).decode('utf-8')

        if read(4) != b'2.00':
            raise ValueError('Expected a DE2 v2 campaign')
        dependency_count = integer()
        if dependency_count > 1024:
            raise ValueError('Invalid campaign dependency count')
        read(dependency_count * 4)
        read(256)
        count = integer()
        if not 0 < count <= 1024:
            raise ValueError('Invalid campaign scenario count')
        entries = []
        for _ in range(count):
            size, offset = integer(), integer()
            title, name = string(), string()
            if not size or offset + size > file_size:
                raise ValueError('Scenario extends beyond campaign bounds')
            entries.append({'name': name, 'title': title, 'offset': offset, 'size': size})
        if any(entry['offset'] < source.tell() for entry in entries):
            raise ValueError('Scenario overlaps campaign table')
    return entries


def extract_scenario(campaign, entry, destination):
    """Extract one table entry to an explicit caller-selected filename."""
    with Path(campaign).open('rb') as source:
        source.seek(entry['offset'])
        remaining = entry['size']
        with Path(destination).open('wb') as output:
            while remaining:
                chunk = source.read(min(65536, remaining))
                if not chunk:
                    raise ValueError('Truncated campaign scenario')
                output.write(chunk)
                remaining -= len(chunk)
