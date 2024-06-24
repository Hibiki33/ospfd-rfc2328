add_rules("mode.debug", "mode.release")

if is_mode("debug") then
    add_defines("DEBUG")
end

set_languages("c++11")

target("ospf")
    set_kind("binary")
    add_files("src/*.cpp")
    add_includedirs("/usr/include")
    add_linkdirs("/usr/lib")
    add_syslinks("pthread")

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
-- ## Change mode
-- $ xmake f -m debug/release
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