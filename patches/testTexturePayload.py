"""Executes the compiled ARM payload in an emulator and compares it with a Python model.

Needs LLVM (see verifyTexturePayload.py) and `pip install unicorn`. No game files are used.
"""
import argparse
import importlib.util
import random
import struct
from pathlib import Path

from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM
from unicorn import arm_const as arm

PayloadBase = 0x2E6200
Sentinel = 0x00700000
StackTop = 0x00900000
Scratch = 0x01000000


def LoadBuilder():
    path = Path(__file__).resolve().parent / "verifyTexturePayload.py"
    spec = importlib.util.spec_from_file_location("verifyTexturePayload", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def TileIndex(x, y, width):
    tile = ((y >> 3) * (width >> 3) + (x >> 3)) << 6
    morton = (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3)
    return tile + morton


def Threshold(x, y, strength):
    column, row = x & 3, y & 3
    mixed = column ^ row
    level = ((mixed & 1) << 3) | ((row & 1) << 2) | (mixed & 2) | ((row >> 1) & 1)
    centered = level * 16 + 8 - 127
    return 127 + ((centered * min(strength, 2)) >> 1)


def Distance(source, first, second, channels):
    result = abs(source[first + 1] - source[second + 1])
    if channels == 4:
        result = max(result, abs(source[first + 3] - source[second + 3]))

    return result


def LocalChange(source, x, y, width, height, channels):
    reach = 8
    here = (y * width + x) * channels
    left = (y * width + x - min(x, reach)) * channels
    right = (y * width + x + min(width - 1 - x, reach)) * channels
    up = ((y - min(y, reach)) * width + x) * channels
    down = ((y + min(height - 1 - y, reach)) * width + x) * channels
    horizontal = min(Distance(source, here, left, channels), Distance(source, here, right, channels))
    vertical = min(Distance(source, here, up, channels), Distance(source, here, down, channels))
    return max(horizontal, vertical)


def AdaptiveThreshold(source, x, y, width, height, channels, fmt, strength):
    limit = 32 if fmt == 1 else 68
    if strength:
        change = LocalChange(source, x, y, width, height, channels)
        if 0 < change <= limit:
            return Threshold(x, y, strength)

    return Threshold(0, 0, 0)


def Quantize(value, maximum, threshold):
    return (value * maximum + threshold) // 255


def Pack(fmt, r, g, b, a, threshold):
    if fmt == 1:
        return (Quantize(r, 31, threshold) << 11) | (Quantize(g, 63, threshold) << 5) | Quantize(b, 31, threshold)

    return ((Quantize(r, 15, threshold) << 12) | (Quantize(g, 15, threshold) << 8) |
            (Quantize(b, 15, threshold) << 4) | Quantize(a, 15, threshold))


class Machine:
    def __init__(self, payload, addresses, strength):
        self.addresses = addresses
        self.uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
        self.uc.ctl_set_cpu_model(arm.UC_CPU_ARM_1176)
        self.uc.mem_map(0x2E6000, 0x2000)
        self.uc.mem_map(StackTop - 0x10000, 0x20000)
        self.uc.mem_map(Scratch, 0x400000)
        self.uc.mem_map(Sentinel, 0x1000)
        self.uc.mem_write(PayloadBase, payload)
        self.uc.mem_write(addresses["DitherStrength"], bytes([strength]))
        # Allow VFP, as on the 3DS ARM11.
        self.uc.reg_write(arm.UC_ARM_REG_C1_C0_2, 0xF00000)
        self.uc.reg_write(arm.UC_ARM_REG_FPEXC, 0x40000000)

    def Call(self, symbol, arguments, stack=()):
        uc = self.uc
        for index, value in enumerate(arguments[:4]):
            uc.reg_write(arm.UC_ARM_REG_R0 + index, value)

        sp = StackTop - 0x100
        for offset, value in enumerate(stack):
            uc.mem_write(sp + 4 * offset, struct.pack("<I", value))

        uc.reg_write(arm.UC_ARM_REG_SP, sp)
        uc.reg_write(arm.UC_ARM_REG_LR, Sentinel)
        uc.emu_start(self.addresses[symbol], Sentinel, count=50_000_000)
        return uc.reg_read(arm.UC_ARM_REG_R0)


def TestSwizzle(payload, addresses):
    rng = random.Random(1234)
    sizes = [(8, 8), (16, 8), (32, 32), (64, 16)]
    checked = 0
    for strength in (0, 1, 2):
        machine = Machine(payload, addresses, strength)
        for fmt, channels in ((1, 3), (2, 4)):
            for width, height in sizes:
                if rng.random() < 0.5:
                    source = bytearray(rng.randrange(256) for _ in range(width * height * channels))
                else:
                    # smooth ramps with a little noise: exercises the dithered branch
                    source = bytearray()
                    for y in range(height):
                        for x in range(width):
                            base = (x * 3 + y * 2 + rng.randrange(3)) & 255
                            source += bytes([min(255, base + 5 * c) for c in range(channels)])
                # Include the extremes so endpoints are checked.
                source[0:channels] = bytes([0] * channels)
                source[channels:2 * channels] = bytes([255] * channels)
                machine.uc.mem_write(Scratch, bytes(source))
                destination = Scratch + 0x100000
                machine.uc.mem_write(destination, b"\xAA" * (width * height * 2))
                result = machine.Call("SwizzleTexture", [fmt, width, height, Scratch], [destination])
                assert result == 1, "SwizzleTexture returned %d" % result
                actual = struct.unpack("<%dH" % (width * height), machine.uc.mem_read(destination, width * height * 2))
                expected = [0] * (width * height)
                position = 0
                for y in range(height):
                    for x in range(width):
                        r, g, b = source[position], source[position + 1], source[position + 2]
                        a = source[position + 3] if fmt == 2 else 255
                        threshold = AdaptiveThreshold(source, x, y, width, height, channels, fmt, strength)
                        expected[TileIndex(x, height - y - 1, width)] = Pack(fmt, r, g, b, a, threshold)
                        position += channels

                assert list(actual) == expected, "Swizzle mismatch: strength %d format %d %dx%d" % (strength, fmt, width, height)
                checked += 1

        # Rejected inputs must leave the buffer alone and return 0.
        assert machine.Call("SwizzleTexture", [1, 12, 8, Scratch], [Scratch + 0x100000]) == 0
        assert machine.Call("SwizzleTexture", [3, 8, 8, Scratch], [Scratch + 0x100000]) == 0

    print("SwizzleTexture matches the model: %d configurations" % checked)


def TestEndpointsAndFlatColors(payload, addresses):
    # Pure black and white must survive every dither strength exactly.
    for strength in (0, 1, 2):
        machine = Machine(payload, addresses, strength)
        for fmt, channels, white in ((1, 3, 0xFFFF), (2, 4, 0xFFFF)):
            for value, expected in ((0, 0 if fmt == 1 else 0), (255, white)):
                source = bytes([value] * (8 * 8 * channels))
                machine.uc.mem_write(Scratch, source)
                machine.Call("SwizzleTexture", [fmt, 8, 8, Scratch], [Scratch + 0x100000])
                pixels = struct.unpack("<64H", machine.uc.mem_read(Scratch + 0x100000, 128))
                assert all(p == expected for p in pixels), "Flat %d not preserved (strength %d, format %d)" % (value, strength, fmt)

    print("Black and white are preserved at every strength")


def TestAdaptive(payload, addresses):
    machine = Machine(payload, addresses, 2)
    uc = machine.uc
    destination = Scratch + 0x100000

    def Convert(fmt, channels, width, height, pixel):
        source = bytearray()
        for y in range(height):
            for x in range(width):
                source += bytes(pixel(x, y))

        uc.mem_write(Scratch, bytes(source))
        machine.Call("SwizzleTexture", [fmt, width, height, Scratch], [destination])
        raw = struct.unpack("<%dH" % (width * height), uc.mem_read(destination, width * height * 2))
        grid = {}
        for y in range(height):
            for x in range(width):
                grid[(x, y)] = raw[TileIndex(x, height - y - 1, width)]

        return grid

    for fmt, channels in ((1, 3), (2, 4)):
        # A flat color that sits between two representable levels must stay perfectly flat.
        flat = Convert(fmt, channels, 32, 32, lambda x, y: [100, 100, 100, 100][:channels])
        assert len(set(flat.values())) == 1, "flat area was patterned (format %d)" % fmt
        # A hard edge must not be dithered either.
        edge = Convert(fmt, channels, 32, 32, lambda x, y: ([0] * channels) if x < 16 else ([255] * channels))
        assert len(set(edge.values())) == 2, "hard edge was patterned (format %d)" % fmt
        # A slow ramp must show both neighbouring levels mixed together (the dither).
        ramp = Convert(fmt, channels, 64, 64, lambda x, y: [(60 + x // 2)] * channels)
        row = [ramp[(x, 20)] for x in range(64)]
        transitions = sum(1 for a, b in zip(row, row[1:]) if a != b)
        plain = Machine(payload, addresses, 0)
        uc.mem_write(Scratch, bytes(bytearray(v for y in range(64) for x in range(64) for v in [60 + x // 2] * channels)))
        plain.uc.mem_write(Scratch, bytes(bytearray(v for y in range(64) for x in range(64) for v in [60 + x // 2] * channels)))
        plain.Call("SwizzleTexture", [fmt, 64, 64, Scratch], [destination])
        raw = struct.unpack("<4096H", plain.uc.mem_read(destination, 8192))
        baseline = [raw[TileIndex(x, 64 - 20 - 1, 64)] for x in range(64)]
        steps = sum(1 for a, b in zip(baseline, baseline[1:]) if a != b)
        assert transitions >= 3 * max(steps, 1), "slow ramp was not dithered (format %d: %d vs %d plain transitions)" % (fmt, transitions, steps)

    print("Flat areas and hard edges stay clean; slow ramps are dithered")


def TestWritePixel(payload, addresses):
    rng = random.Random(99)
    width, height = 16, 16
    for strength in (0, 2):
        machine = Machine(payload, addresses, strength)
        uc = machine.uc
        vtable, texture, pixels, colorAddress = Scratch, Scratch + 0x1000, Scratch + 0x2000, Scratch + 0x3000
        widthStub, heightStub = Scratch + 0x4000, Scratch + 0x4010

        def Stub(address, value):
            uc.mem_write(address, struct.pack("<II", 0xE3A00000 | value, 0xE12FFF1E))  # mov r0,#value; bx lr

        Stub(widthStub, width)
        Stub(heightStub, height)
        uc.mem_write(vtable + 48, struct.pack("<II", widthStub, heightStub))
        for fmt in (1, 2):
            uc.mem_write(texture, struct.pack("<I", vtable))
            uc.mem_write(texture + 24, bytes([fmt]))
            uc.mem_write(texture + 108, struct.pack("<I", pixels))
            for _ in range(40):
                x, y = rng.randrange(width), rng.randrange(height)
                color = [rng.random() for _ in range(3)] + [rng.random()]
                if rng.random() < 0.2:
                    color[rng.randrange(4)] = rng.choice([0.0, 1.0, 1.7])  # exercises the clamp
                uc.mem_write(colorAddress, struct.pack("<4f", *color))
                assert machine.Call("WritePixel", [texture, x, y, colorAddress]) == 1
                address = pixels + 2 * TileIndex(x, height - y - 1, width)
                actual = struct.unpack("<H", uc.mem_read(address, 2))[0]
                channels = []
                for component in color:
                    product = struct.unpack("<f", struct.pack("<f", struct.unpack("<f", struct.pack("<f", component))[0] * 255.0))[0]
                    channels.append(min(255, max(0, int(product))))

                expected = Pack(fmt, *channels, Threshold(0, 0, 0))
                assert actual == expected, "WritePixel mismatch %04x != %04x" % (actual, expected)

    print("WritePixel matches the model, including clamping")


def Main():
    parser = argparse.ArgumentParser(description="Emulate the texture payload and check it against a Python model")
    parser.add_argument("--output", type=Path, default=Path("build/payload-test"))
    arguments = parser.parse_args()
    builder = LoadBuilder()
    patchPath = Path(__file__).resolve().parent
    payload, addresses = builder.BuildPayload(patchPath, arguments.output.resolve())
    TestSwizzle(payload, addresses)
    TestEndpointsAndFlatColors(payload, addresses)
    TestAdaptive(payload, addresses)
    TestWritePixel(payload, addresses)
    print("Payload emulation tests passed")


if __name__ == "__main__":
    Main()
