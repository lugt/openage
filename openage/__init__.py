# Copyright 2013-2026 the openage authors. See copying.md for legal info.

"""
The Python part of openage, a free engine re-write of
Age of Empires II.

See https://openage.dev and http://github.com/SFTtech/openage

Requires Python 3.6.
"""



def _bootstrap_build_dir():
    """Redirect generated/compiled modules to the build directory.

    `python -m openage` may resolve to the *source* package (e.g. when run
    from the repository root) which lacks the codegen-generated ``config.py``
    and the compiled Cython ``.so`` modules -- those live only in the build
    directory's openage package copy.

    When that happens:
      * the build package is appended to ``__path__`` so top-level generated
        modules (``config``) and compiled modules (``cython_check``) resolve
        there;
      * a meta-path finder is installed so any ``openage.*`` submodule whose
        source tree carries only a ``.pyx`` (a Cython module with no loadable
        ``.py`` form) is loaded from the build directory's compiled ``.so``
        instead.

    Pure-python modules still load from the live source tree, so in-place
    edits take effect without rebuilding.  Override the auto-detected
    directory with the ``OPENAGE_BUILD_DIR`` environment variable.  This is a
    no-op when already importing from a complete (build) package.
    """
    import importlib.machinery
    import os
    import sys

    pkg_dir = os.path.dirname(os.path.abspath(__file__))
    if os.path.isfile(os.path.join(pkg_dir, "config.py")):
        return

    repo_root = os.path.dirname(pkg_dir)
    candidates = [os.environ.get("OPENAGE_BUILD_DIR")]
    for name in ("bin", "build", "build-debug", "build-release"):
        candidates.append(os.path.join(repo_root, name))
    binroot = os.path.join(repo_root, ".bin")
    if os.path.isdir(binroot):
        candidates.extend(
            os.path.join(binroot, sub) for sub in sorted(os.listdir(binroot))
        )

    build_pkg = None
    for candidate in candidates:
        if not candidate:
            continue
        pkg = os.path.join(candidate, "openage")
        if os.path.isfile(os.path.join(pkg, "config.py")):
            build_pkg = pkg
            break
    if build_pkg is None:
        return

    # top-level generated/compiled modules resolve via the build package
    # appended to our __path__.
    if build_pkg not in __path__:
        __path__.append(build_pkg)

    # Subpackages load from the source tree, so their own __path__ does not
    # include the build directory -- compiled submodules (e.g. log.log_cpp,
    # convert.processor.export.terrain_merge) would not be found.  Fall back
    # to the build-dir .so for any openage.* module that is .pyx-only in the
    # source tree.
    src_root = pkg_dir

    class _CompiledFallback:
        """Load build-dir .so for openage.* modules that are .pyx-only in source."""

        def find_spec(self, fullname, path=None, target=None):
            parts = fullname.split(".")
            if len(parts) < 2 or parts[0] != "openage":
                return None
            rel = os.path.join(*parts[1:])
            if not os.path.exists(os.path.join(src_root, rel + ".pyx")):
                return None
            # a loadable source form exists -> let the default finder handle it
            if (os.path.exists(os.path.join(src_root, rel + ".py"))
                    or os.path.exists(os.path.join(src_root, rel, "__init__.py"))):
                return None
            parent = os.path.dirname(rel)
            search_dir = os.path.join(build_pkg, parent) if parent else build_pkg
            if not os.path.isdir(search_dir):
                return None
            return importlib.machinery.PathFinder().find_spec(
                fullname, [search_dir]
            )

    if not any(isinstance(finder, _CompiledFallback) for finder in sys.meta_path):
        sys.meta_path.insert(0, _CompiledFallback())


_bootstrap_build_dir()


from sys import version_info as py_version

from .log import setup_logging

if py_version < (3, 9):
    raise RuntimeError("openage requires python 3.9 or higher.")


try:
    # TODO pylint: disable=wrong-import-position,import-self
    from . import config

except ImportError:
    VERSION = "< unknown version; ./configure incomplete >"
    LONGVERSION = VERSION

else:
    VERSION = config.VERSION

    LONGVERSION = (
        f"openage {config.VERSION}"
        f"{' [devmode]' if config.DEVMODE else ''}\n"
        f"{config.CONFIG_OPTIONS}, ci-cfg {config.CICFGVERSION}\n"
        f"{config.COMPILER} [{config.COMPILERFLAGS}]\n"
        "\n"
        "== Python ==\n"
        f"Python        {config.PYTHONINTERPRETER}\n"
        f"Python C API  {config.PYTHONCAPI}\n"
        f"Cython        {config.CYTHONVERSION}\n"
        f"Mako          {config.MAKOVERSION}\n"
        f"NumPy         {config.NUMPYVERSION}\n"
        f"Pillow        {config.PILVERSION}\n"
        f"Pygments      {config.PYGMENTSVERSION}"
    )


setup_logging()
