set_project("nes-bench")
set_xmakever("3.0.0")
add_rules("mode.release")
set_languages("c11")
set_warnings("allextra")
add_cflags("/utf-8", {tools = "cl"})

local function common()
    set_kind("binary")
    add_includedirs(".", "../inc")
    add_files("../src/**.c", "main.c")
    add_defines(
        "NES_TEST_MODE=1",
        "NES_TEST_PROFILE=1",
        "NES_ENABLE_SOUND=1",
        "NES_USE_FS=1",
        "NES_ENABLE_HEAVY_MAPPERS=1",
        "NES_ENABLE_PLANE1_MAPPERS=1",
        "NES_ENABLE_PLANE2_MAPPERS=1"
    )
    add_defines("_CRT_SECURE_NO_WARNINGS", {tools = "cl"})
    set_targetdir("out/bin")
end

target("nes-bench-ref", function()
    common()
    add_defines(
        "NES_USE_SRAM=1",
        "NES_COLOR_DEPTH=32",
        "NES_RAM_LACK=0",
        "NES_ROM_STREAM=0"
    )
end)

target("nes-bench-mcu", function()
    common()
    add_defines(
        "NES_USE_SRAM=0",
        "NES_COLOR_DEPTH=16",
        "NES_RAM_LACK=1",
        "NES_ROM_STREAM=1",
        "NES_FRAME_SKIP=1"
    )
end)

target("nes-bench-mcu-cache", function()
    common()
    add_defines("NES_USE_SRAM=0", "NES_COLOR_DEPTH=16", "NES_RAM_LACK=1",
                "NES_ROM_STREAM=1", "NES_FRAME_SKIP=1",
                "NES_PRG_CACHE_SLOTS=8", "NES_CHR_CACHE_SLOTS=32")
end)
