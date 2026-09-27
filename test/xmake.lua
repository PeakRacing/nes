set_project("nes-tests")
set_xmakever("3.0.0")
set_languages("c11")
set_warnings("allextra")
add_cflags("/utf-8", {tools = "cl"})

local function common()
    set_kind("binary")
    add_includedirs("../inc", "runner", "unit")
    add_files("../src/**.c", "runner/*.c", "unit/*.c", "stress/*.c")
    add_defines("NES_TEST_MODE=1", "NES_ENABLE_SOUND=1", "NES_USE_SRAM=1", "NES_USE_FS=1", "NES_ENABLE_HEAVY_MAPPERS=1", "NES_ENABLE_PLANE1_MAPPERS=1", "NES_ENABLE_PLANE2_MAPPERS=1")
    add_defines("_CRT_SECURE_NO_WARNINGS", {tools = "cl"})   -- fopen/fseek are fine here
    set_targetdir("out/bin")
end

target("nes-tests", function () common(); add_defines("NES_ROM_STREAM=0") end)
target("nes-tests-stream", function () common(); add_defines("NES_ROM_STREAM=1") end)
