"""Bounded-memory DAT adapter around the pinned upstream schema reader."""

from collections.abc import Sequence
from contextlib import contextmanager
import hashlib
import mmap
from tempfile import TemporaryFile
import zlib


CHUNK_SIZE = 1024 * 1024


def inflate(source, destination):
    """Inflate raw DEFLATE in bounded chunks and hash the original bytes."""
    digest = hashlib.sha256()
    inflater = zlib.decompressobj(-15)
    while chunk := source.read(CHUNK_SIZE):
        digest.update(chunk)
        pending = chunk
        while pending:
            destination.write(inflater.decompress(pending, CHUNK_SIZE))
            pending = inflater.unconsumed_tail
        if inflater.unused_data:
            raise ValueError("Unexpected trailing data after the DAT stream")
    if not inflater.eof:
        raise ValueError("Truncated DAT compressed stream")
    destination.write(inflater.flush())
    destination.flush()
    return digest.hexdigest()


@contextmanager
def read_dat(path):
    """Keep global tables and one civilization in memory at a time.

    The upstream parser supplies every field and version-specific rule. Its
    civilization-array read is intercepted to record disk offsets, releasing
    each civilization after indexing. A second pass reads those records lazily
    once technology tables, which follow civilizations in DAT, are available.
    """
    from genieutils.civ import Civ
    from genieutils.common import ByteHandler
    from genieutils.datfile import DatFile

    class Civilizations(Sequence):
        def __init__(self, content, version, offsets):
            self.content = content
            self.version = version
            self.offsets = offsets

        def __len__(self):
            return len(self.offsets)

        def __getitem__(self, index):
            if isinstance(index, slice):
                raise TypeError("Use individual civilizations to preserve bounded memory")
            reader = ByteHandler(self.content)
            reader.version = self.version
            reader.offset = self.offsets[index]
            return Civ.from_bytes(reader)

    class IndexingReader(ByteHandler):
        def read_class_array(self, cls, size):
            if cls is not Civ:
                return super().read_class_array(cls, size)
            offsets = []
            for index in range(size):
                offsets.append(self.offset)
                # No list retains these large unit arrays.
                Civ.from_bytes(self)
                print(f"Indexed civilization {index + 1}/{size}", flush=True)
            return Civilizations(self.content, self.version, offsets)

    with path.open("rb") as source, TemporaryFile() as temporary:
        checksum = inflate(source, temporary)
        size = temporary.tell()
        with mmap.mmap(temporary.fileno(), 0, access=mmap.ACCESS_READ) as mapped:
            content = memoryview(mapped)
            try:
                reader = IndexingReader(content)
                data = DatFile.from_bytes(reader)
                if reader.offset != size:
                    raise ValueError(f"DAT was not fully consumed: {reader.offset}/{size} bytes")
                yield data, {
                    "file": path.name, "sha256": checksum, "version": data.version,
                    "uncompressed_bytes": size, "parsed_bytes": reader.offset,
                    "reader": "genieutils-py==0.1.2", "civilization_read_mode": "lazy-two-pass",
                    "inflate_chunk_bytes": CHUNK_SIZE,
                }
            finally:
                content.release()
