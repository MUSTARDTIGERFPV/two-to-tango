"""Fill image_size and image_crc32 in an ELRS image header (abi/elrs_image.h).

  python finish_image.py <image.bin>
Header layout: magic u32, abi u16 u16, image_size u32 @8, image_crc32 u32 @12;
the CRC covers [sizeof(header), image_size). sizeof(header) = 60 (ELRS_IMAGE_HEADER_SIZE).
"""
import struct
import sys
import zlib

HEADER_SIZE = 60


def main(path):
    img = bytearray(open(path, "rb").read())
    magic, = struct.unpack_from("<I", img, 0)
    if magic != 0x3252454C:
        sys.exit("not an ELRS image (magic %08x)" % magic)
    struct.pack_into("<I", img, 8, len(img))
    struct.pack_into("<I", img, 12, zlib.crc32(bytes(img[HEADER_SIZE:])) & 0xFFFFFFFF)
    open(path, "wb").write(img)
    print("%s: %d bytes, crc %08x" % (path, len(img), struct.unpack_from("<I", img, 12)[0]))


if __name__ == "__main__":
    main(sys.argv[1])
