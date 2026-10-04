"""Official Python bindings for the Chronon3D C ABI (ctypes, no compiled extension).

Library discovery order: ``CHRONON3D_LIB`` env var (file or directory) →
``pkg-config --variable=libdir chronon3d`` → system loader search.
"""

from .engine import (Frame, Plan, RenderEngine, abi_version, contract_version, status_name,
                     version)
from .errors import *  # noqa: F401,F403
from .errors import ChrononError, Status, error_for

__all__ = ["RenderEngine", "Plan", "Frame", "ChrononError", "Status", "error_for",
           "abi_version", "contract_version", "version", "status_name"]
__version__ = "0.1.0"
