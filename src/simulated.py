"""In-memory backend with scripted receipts and synchronization for tests."""

from collections import deque
from collections.abc import Callable, Iterable
from errno import ECANCELED, ETIMEDOUT
from threading import Event
from time import monotonic

from transport import LG_GRAM_GOODIX, DeviceIdentity, Message, Receipt


class SimulatedBackend:
    def __init__(
        self,
        receipts: Iterable[Receipt],
        *,
        identity: DeviceIdentity = LG_GRAM_GOODIX,
        before_reply: Callable[[int, float, Event], None] | None = None,
    ):
        self._identity = identity
        self._receipts = deque(receipts)
        self._before_reply = before_reply
        self.messages: list[Message] = []

    @property
    def identity(self) -> DeviceIdentity:
        return self._identity

    def exchange(
        self, message: Message, *, deadline: float, stopped: Event
    ) -> Receipt:
        if stopped.is_set():
            return Receipt(-ECANCELED, 0)
        if monotonic() >= deadline:
            return Receipt(-ETIMEDOUT, 0)
        if not self._receipts:
            raise AssertionError("unexpected SPI message: script exhausted")
        receipt = self._receipts.popleft()
        self.messages.append(message)
        if self._before_reply is not None:
            self._before_reply(len(self.messages), deadline, stopped)
        return receipt
