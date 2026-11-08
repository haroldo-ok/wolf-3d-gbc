"""gbpack.py - packs data blobs into 16 KB ROM banks (addresses 0x4000..0x7FFF)."""

BANK = 0x4000


class Packer:
    def __init__(self, first_bank):
        self.first = first_bank
        self.banks = []          # list of bytearray
        self.names = {}

    def _new(self):
        self.banks.append(bytearray())
        return len(self.banks) - 1

    def add(self, name, blob, align=1):
        """place blob (<= 16 KB) in a bank without crossing: returns (bank, addr)"""
        blob = bytes(blob)
        assert len(blob) <= BANK, (name, len(blob))
        for i, b in enumerate(self.banks):
            off = (len(b) + align - 1) // align * align
            if off + len(blob) <= BANK and i >= self._start_search:
                b.extend(bytes(off - len(b)))
                b.extend(blob)
                self.names[name] = (self.first + i, 0x4000 + off)
                return self.names[name]
        i = self._new()
        self.banks[i].extend(blob)
        self.names[name] = (self.first + i, 0x4000)
        return self.names[name]

    _start_search = 0

    def add_bank(self, name, blob):
        """a whole bank of its own"""
        blob = bytes(blob)
        assert len(blob) <= BANK, (name, len(blob))
        i = self._new()
        self.banks[i].extend(blob)
        self.names[name] = (self.first + i, 0x4000)
        self._start_search = len(self.banks)
        return self.names[name]

    def seal(self):
        """later add() calls start in new banks"""
        self._start_search = len(self.banks)

    def add_stream(self, name, blob):
        """a long stream split over consecutive banks: returns (first bank, 0x4000); the stream
        continues at 0x4000 of the next bank"""
        self.seal()
        first = None
        for o in range(0, len(blob), BANK):
            i = self._new()
            self.banks[i].extend(blob[o:o + BANK])
            if first is None:
                first = self.first + i
        self.names[name] = (first, 0x4000)
        self.seal()
        return self.names[name]

    def image(self):
        out = bytearray()
        for b in self.banks:
            out += b + bytes(BANK - len(b))
        return bytes(out)

    @property
    def nbanks(self):
        return len(self.banks)
