set_project("nes")
set_xmakever("3.0.0")
add_rules("mode.debug", "mode.release")

if is_mode("debug") then
    set_symbols("debug")
    set_optimize("none")
else
    set_strip("all")
    set_symbols("hidden")
    set_optimize("fastest")
end

set_warnings("allextra")
set_languages("c11")
add_cflags("/utf-8", {tools = "cl"})

if is_plat("wasm") then
    add_requires("emscripten")
    set_toolchains("emcc@emscripten")
end

-- TODO
-- if is_host("windows") then
-- elseif is_host("linux") then
-- elseif is_host("macosx") then
-- else 
-- end

-- [[ add SDL2 ]]
add_requires("libsdl2", {configs = {sdlmain = false}})
add_packages("libsdl2")

-- Every source file is compiled in: test/debug scaffolding (src/nes_test.c and the hooks in
-- src/nes_cpu.c, port/nes_port.c) is wrapped in "#if defined(NES_TEST_MODE) && (NES_TEST_MODE == 1)"
-- and therefore produces no code at all unless a target defines that macro.  No file has to be
-- excluded from the build anymore.
local function nes_sources()
    local nes_dir = "../.."
    add_includedirs(nes_dir .. "/inc")
    add_files(nes_dir .. "/src/**.c")

    add_includedirs("port")
    add_files("port/*.c")

    add_files("main.c")
end

target("nes", function ()
    set_kind("binary")
    nes_sources()
end)

-- Same emulator with the macro isolated test hooks enabled, used by the automated save-state
-- checks: it honours NES_TEST_SAVE_AT / NES_TEST_LOAD_AT / NES_TEST_EXIT_AT / NES_TEST_HASHLOG
-- so the F5/F8 code paths can be driven without synthetic keystrokes.  The production "nes"
-- target never defines NES_TEST_MODE, so it stays free of test code.
target("nes-test", function ()
    set_kind("binary")
    set_basename("nes-test")

    nes_sources()

    add_defines("NES_TEST_MODE=1")
end)
