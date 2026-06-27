# Minimal dependency-free white grayscale PNG writer (stored/uncompressed deflate).
# usage: julia make_white_png.jl <out.png> <width> <height>
function crc32(data::Vector{UInt8})
    crc = 0xffffffff
    for b in data
        crc ⊻= UInt32(b)
        for _ in 1:8
            crc = (crc & 0x1) != 0 ? (0xedb88320 ⊻ (crc >> 1)) : (crc >> 1)
        end
    end
    return crc ⊻ 0xffffffff
end
be32(x) = UInt8[(x>>24)&0xff, (x>>16)&0xff, (x>>8)&0xff, x&0xff]
function chunk(io, typ::String, data::Vector{UInt8})
    write(io, be32(UInt32(length(data))))
    body = vcat(collect(codeunits(typ)), data)
    write(io, body); write(io, be32(crc32(body)))
end
function main(path, w, h)
    raw = UInt8[]
    for _ in 1:h; push!(raw, 0x00); append!(raw, fill(0xff, w)); end   # filter 0 + white row
    # zlib: header + stored deflate blocks + adler32
    zlib = UInt8[0x78, 0x01]
    i = 1; n = length(raw)
    while i <= n
        L = min(65535, n - i + 1)
        push!(zlib, (i + L - 1 == n) ? 0x01 : 0x00)
        push!(zlib, L & 0xff, (L >> 8) & 0xff)
        nl = (~UInt16(L)) & 0xffff
        push!(zlib, nl & 0xff, (nl >> 8) & 0xff)
        append!(zlib, raw[i:i+L-1]); i += L
    end
    a = UInt32(1); b = UInt32(0)
    for x in raw; a = (a + x) % 65521; b = (b + a) % 65521; end
    append!(zlib, be32((b << 16) | a))
    open(path, "w") do io
        write(io, UInt8[0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a])
        chunk(io, "IHDR", vcat(be32(UInt32(w)), be32(UInt32(h)), UInt8[8,0,0,0,0]))  # 8-bit grayscale
        chunk(io, "IDAT", zlib)
        chunk(io, "IEND", UInt8[])
    end
end
main(ARGS[1], parse(Int, ARGS[2]), parse(Int, ARGS[3]))
