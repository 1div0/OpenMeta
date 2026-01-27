"""
OpenMeta Python bindings.

The public API is implemented in the compiled extension module `_openmeta`.
"""

from . import _openmeta as _impl  # noqa: F401
from ._openmeta import *  # noqa: F401,F403

__version__ = getattr(_impl, "__version__", "0.0.0")
