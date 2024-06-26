add_rules("mode.debug", "mode.release")

if is_mode("debug") then
    add_defines("DEBUG")
end

set_languages("c++11")

option("static")
    set_default(false)
    set_showmenu(true)
    set_description("Build static binary")
    add_defines("STATIC")

target("ospf")
    set_kind("binary")
    add_files("src/*.cpp")
    add_includedirs("/usr/include")
    add_linkdirs("/usr/lib")
    add_syslinks("pthread")

    if has_config("static") then
        add_ldflags("-static", "-static-libgcc", "-static-libstdc++")
    end

    -- define router configuration
    add_defines(
        "OSPF_VERSION=2",
        "THIS_ROUTER_NAME=\"R0\"",
        "THIS_ROUTER_ID=\"1.1.1.1\""
    )

task("fix-style")
    set_category("plugin")
    on_run(function ()
        os.exec("bash style.sh -f")
    end)
    set_menu({
        usage = "xmake fix-style",
        description = "Format C++ code with clang-format"
    })

-- 
-- ## Change mode, options
-- $ xmake f -m debug/release --static=true/false
--
-- ## Build project
-- $ xmake
-- 
-- ## Run target
-- $ xmake run
--
-- ## Format code
-- $ xmake fix-style
--