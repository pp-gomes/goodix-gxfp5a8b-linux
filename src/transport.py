"""SPI message and backend contracts for the offline diagnostic."""

from dataclasses import dataclass
from threading import Event
from typing import Protocol


@dataclass(frozen=True)
class DeviceIdentity:
    hid: str
    acpi_path: str
    resource_source: str
    controller_pci_id: str
    chip_select: int
    mode: int
    bits_per_word: int
    max_speed_hz: int
    active_low: bool
    four_wire: bool


LG_GRAM_GOODIX = DeviceIdentity(
    hid="GXFP5A8B",
    acpi_path=r"\_SB.PCI0.SPI1.FPNT",
    resource_source=r"\_SB.PCI0.SPI1",
    controller_pci_id="8086:34ab",
    chip_select=0,
    mode=0,
    bits_per_word=8,
    max_speed_hz=10_000_000,
    active_low=True,
    four_wire=True,
)


@dataclass(frozen=True)
class Transfer:
    tx: bytes = b""
    rx_length: int = 0


@dataclass(frozen=True)
class Message:
    """One CS assertion across all transfers, with deassertion at the end."""

    transfers: tuple[Transfer, ...]

    @property
    def wire_length(self) -> int:
        return sum(len(t.tx) + t.rx_length for t in self.transfers)


@dataclass(frozen=True)
class Receipt:
    status: int
    actual_length: int
    rx: bytes = b""


class Backend(Protocol):
    @property
    def identity(self) -> DeviceIdentity: ...

    def exchange(
        self, message: Message, *, deadline: float, stopped: Event
    ) -> Receipt:
        """Submit one message synchronously, with no retries.

        Report status (zero or negative errno), confirmed bytes clocked across
        TX and RX, and only confirmed RX bytes. Return after releasing buffers.
        Use the shared monotonic deadline and stop event cooperatively.
        """
        ...
