"""Version-of-CPU diagnostic for the documented LG Gram profile."""

from dataclasses import dataclass
from enum import Enum, auto
from errno import ETIMEDOUT
from math import isfinite
from threading import Event, Lock
from time import monotonic

from transport import LG_GRAM_GOODIX, Backend, Message, Receipt, Transfer


SELECT_CPU_ADDRESS = Message((Transfer(tx=b"\xf0\x00\xa8"),))
READ_CPU_VERSION = Message((Transfer(tx=b"\xf1"), Transfer(rx_length=2)))


class Outcome(Enum):
    VALID_REFERENCE = auto()
    INVALID_RESPONSE = auto()
    TRANSPORT_ERROR = auto()
    LENGTH_MISMATCH = auto()
    TIMEOUT = auto()
    CANCELLED = auto()
    REMOVED = auto()


@dataclass(frozen=True)
class Result:
    outcome: Outcome
    receipts: tuple[Receipt, ...]


class CpuDiagnostic:
    """One session per target. Serialize the selector and read together."""

    def __init__(self, backend: Backend):
        if backend.identity != LG_GRAM_GOODIX:
            raise ValueError("backend does not match the LG Gram Goodix SPI resource")
        self._backend = backend
        self._operation = Lock()
        self._state = Lock()
        self._removed = False
        self._active: Event | None = None

    def cancel(self) -> bool:
        """Signal the active operation only, leaving queued calls eligible."""
        with self._state:
            if self._active is None or self._removed:
                return False
            self._active.set()
            return True

    def remove(self) -> None:
        """Permanently close admission and drain any active backend call."""
        with self._state:
            self._removed = True
            if self._active is not None:
                self._active.set()
        with self._operation:
            pass

    def _stop_reason(self, stopped: Event, deadline: float) -> Outcome | None:
        # The caller holds _state.
        if self._removed:
            return Outcome.REMOVED
        if stopped.is_set():
            return Outcome.CANCELLED
        if monotonic() >= deadline:
            return Outcome.TIMEOUT
        return None

    def read(self, *, timeout: float) -> Result:
        """Use one timeout budget across both messages, starting at admission."""
        if not isfinite(timeout) or timeout <= 0:
            raise ValueError("timeout must be finite and positive")
        with self._operation:
            with self._state:
                if self._removed:
                    return Result(Outcome.REMOVED, ())
                stopped = Event()
                self._active = stopped
                deadline = monotonic() + timeout

            receipts: list[Receipt] = []
            try:
                outcome = self._read_messages(receipts, deadline, stopped)
                with self._state:
                    outcome = self._stop_reason(stopped, deadline) or outcome
                    self._active = None
                    return Result(outcome, tuple(receipts))
            finally:
                with self._state:
                    self._active = None

    def _read_messages(
        self, receipts: list[Receipt], deadline: float, stopped: Event
    ) -> Outcome:
        for message, rx_length in ((SELECT_CPU_ADDRESS, 0), (READ_CPU_VERSION, 2)):
            with self._state:
                reason = self._stop_reason(stopped, deadline)
            if reason is not None:
                return reason
            receipt = self._backend.exchange(message, deadline=deadline, stopped=stopped)
            receipts.append(receipt)
            if receipt.status == -ETIMEDOUT:
                return Outcome.TIMEOUT
            if receipt.status != 0:
                return Outcome.TRANSPORT_ERROR
            if receipt.actual_length != message.wire_length or len(receipt.rx) != rx_length:
                return Outcome.LENGTH_MISMATCH

        if receipts[-1].rx == b"\x00\xa1":
            return Outcome.VALID_REFERENCE
        return Outcome.INVALID_RESPONSE
