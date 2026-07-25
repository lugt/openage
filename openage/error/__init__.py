# Copyright 2026 the openage authors. See copying.md for legal info.
"""
openage base exception class.

The corresponding C++ class lives in ``libopenage/error/error.h`` (see
``class Error : public std::runtime_error``). The Python wrapper was
missing from the source tree at ``openage/error/`` despite the
existing call sites

    openage/main/main.py:172
    openage/game/main.py:121

which both do ``from ..error import Error as _Error``. The intent is
to expose the same exception type to Python so callers can ``raise``
or ``except`` the openage error class uniformly across languages.

This is a minimal Python re-implementation: the message-accepting
constructor and a ``type_name()`` helper that mirrors the C++ side
(useful for log messages). It is NOT a Cython wrapper of the C++
``Error`` because no ``error_cpp.pyx`` ever existed in the source
tree (see `git log --all -S "error_cpp"` — empty). The previous
"refactor: Move OpenGL error checks into renderer" commit only
removed a different ``gl_debug`` pair; the Python entry point was
simply never added.

If a future commit introduces a Cython wrapper, the public API
below is what callers depend on, so it must stay compatible.
"""
from __future__ import annotations


class Error(Exception):
    """
    openage base exception.

    Mirrors the C++ ``openage::error::Error`` constructor signature:

        Error(const message &msg)

    by accepting a single message string. The message is stored
    as ``args[0]`` (Python idiom) and also accessible via
    ``str(self)`` and ``self.msg``.
    """

    def __init__(self, msg: object = "") -> None:
        # ``Exception`` takes ``*args``; we always pass a single
        # message so ``self.args == (msg,)`` and ``str(self) == msg``
        # for a single-arg construction.
        super().__init__(str(msg))
        self.msg: str = str(msg)

    def type_name(self) -> str:
        """Mirror of C++ ``Error::type_name()``; for log messages."""
        return type(self).__name__

    def __repr__(self) -> str:
        return f"{self.type_name()}({self.msg!r})"
