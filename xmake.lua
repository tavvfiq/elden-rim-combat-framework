-- set minimum xmake version
set_xmakever("2.8.2")

-- Make sure xmake can find the local CommonLibSSE-NG xmake scripts (plugin rule).
includes("D:/Modding/CommonLibSSE-NG")

-- set project
set_project("ERCF")
set_version("0.0.0")
set_license("GPL-3.0")

-- set defaults
set_languages("c++23")
set_warnings("allextra")

-- clangd / LSP: `xmake project -k compile_commands` → compile_commands.json in repo root (see .clangd)

-- config deps
add_requires("toml11")

-- add rules
add_rules("mode.debug", "mode.releasedbg")
set_defaultmode("releasedbg")

-- targets
target("ercf")
    add_deps("commonlibsse-ng")
    add_packages("toml11")
    add_rules("commonlibsse-ng.plugin", {
        name = "ERCF",
        author = "taufiq.nugroho",
        description = "Elden Rim Combat Framework (ERCF)",
        email = "unknown@example.com"
    })

    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    set_pcxxheader("src/pch.h")

target("combat_math_tests")
    set_kind("binary")
    add_files("tests/**.cpp")
    add_includedirs("src")

