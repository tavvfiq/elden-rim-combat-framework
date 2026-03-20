-- set minimum xmake version
set_xmakever("2.8.2")

-- Locate local CommonLibSSE-NG xmake scripts (plugin rule).
--
-- Override via:
--   xmake f --commonlibsse_dir="D:/path/to/CommonLibSSE-NG"
option("commonlibsse_dir")
    set_default("../CommonLibSSE-NG")
    set_showmenu(true)
    set_description("Path to local CommonLibSSE-NG checkout")
option_end()

local commonlib_dir = get_config("commonlibsse_dir") or "../CommonLibSSE-NG"
if os.isdir(commonlib_dir) then
    includes(path.join(commonlib_dir, "xmake.lua"))
else
    raise("CommonLibSSE-NG not found at '" .. commonlib_dir .. "'. Configure with: xmake f --commonlibsse_dir=\"D:/path/to/CommonLibSSE-NG\"")
end

-- set project
set_project("ERCF")
set_version("0.0.0")
set_license("GPL-3.0")

-- set defaults
set_languages("c++23")
set_warnings("allextra")

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

