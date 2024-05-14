add_rules("mode.debug", "mode.release")

set_languages("c++17")

target("ospf")
    set_kind("binary")
    add_files("src/*.cpp")
    add_includedirs("/usr/include")
    add_linkdirs("/usr/lib")
    add_syslinks("pthread")

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