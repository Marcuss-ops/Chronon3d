"""Exception hierarchy mapped 1:1 from ``chronon_status`` codes."""

from __future__ import annotations

from enum import IntEnum


class Status(IntEnum):
    OK = 0
    UNKNOWN = 1
    INVALID_ARGUMENT = 2
    PARSE_FAILED = 3
    RENDER_FAILED = 4
    IO_FAILED = 5
    UNSUPPORTED = 6
    CANCELLED = 7
    ABI_MISMATCH = 8
    BUFFER_TOO_SMALL = 9
    BUSY = 10
    ASSET_CHANGED = 11
    BUDGET_EXCEEDED = 12
    PREFLIGHT_FAILED = 13
    INVALID_PLAN = 14
    UNSUPPORTED_SCHEMA = 15
    ASSET_NOT_FOUND = 16
    DECODE_FAILED = 17
    ENCODE_FAILED = 18
    OUT_OF_MEMORY = 19


class ChrononError(Exception):
    """Base error. Carries the C status plus the structured error-info fields."""

    status: Status = Status.UNKNOWN

    def __init__(self, message: str = "", *, status: int | None = None, code: str | None = None,
                 component: str | None = None, node_id: str | None = None, asset: str | None = None):
        if status is not None:
            try:
                self.status = Status(status)
            except ValueError:
                self.status = Status.UNKNOWN
        self.message = message
        self.code = code
        self.component = component
        self.node_id = node_id
        self.asset = asset
        super().__init__(f"[{self.status.name}] {message}" if message else self.status.name)


class InvalidArgumentError(ChrononError, ValueError): status = Status.INVALID_ARGUMENT
class ParseError(ChrononError): status = Status.PARSE_FAILED
class RenderError(ChrononError): status = Status.RENDER_FAILED
class ChrononIOError(ChrononError, OSError): status = Status.IO_FAILED
class UnsupportedError(ChrononError): status = Status.UNSUPPORTED
class CancelledError(ChrononError): status = Status.CANCELLED
class AbiMismatchError(ChrononError): status = Status.ABI_MISMATCH
class BufferTooSmallError(ChrononError): status = Status.BUFFER_TOO_SMALL
class BusyError(ChrononError): status = Status.BUSY
class AssetChangedError(ChrononError): status = Status.ASSET_CHANGED
class BudgetExceededError(ChrononError): status = Status.BUDGET_EXCEEDED
class PreflightError(ChrononError): status = Status.PREFLIGHT_FAILED
class InvalidPlanError(ChrononError): status = Status.INVALID_PLAN
class UnsupportedSchemaError(ChrononError): status = Status.UNSUPPORTED_SCHEMA
class AssetNotFoundError(ChrononError): status = Status.ASSET_NOT_FOUND
class DecodeError(ChrononError): status = Status.DECODE_FAILED
class EncodeError(ChrononError): status = Status.ENCODE_FAILED
class ChrononMemoryError(ChrononError, MemoryError): status = Status.OUT_OF_MEMORY

_BY_STATUS = {cls.status: cls for cls in ChrononError.__subclasses__()}


def error_for(status: int, message: str = "", **fields) -> ChrononError:
    cls = _BY_STATUS.get(status, ChrononError)
    return cls(message, status=status, **fields)
