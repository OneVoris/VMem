set_project("VMem")
set_version("0.1.0")
set_xmakever("3.0.0")
set_languages("cxx23")
set_warnings("allextra")
add_rules("mode.debug", "mode.release")

option("build_shared")
    set_default(false)
    set_showmenu(true)
option_end()

option("build_tests")
    set_default(false)
    set_showmenu(true)
option_end()

option("build_examples")
    set_default(false)
    set_showmenu(true)
option_end()

option("build_benchmarks")
    set_default(false)
    set_showmenu(true)
option_end()

option("build_fuzzers")
    set_default(false)
    set_showmenu(true)
option_end()

option("with_voris_dependencies")
    set_default(false)
    set_showmenu(true)
    set_description("Resolve released Voris dependencies through VXrepo")
option_end()

if has_config("with_voris_dependencies") then
    -- This repository has no internal upstream package.
end

target("voris_vmem")
    if has_config("build_shared") then
        set_kind("shared")
        add_defines("VORIS_VMEM_SHARED", {public = true})
    else
        set_kind("static")
    end
    add_headerfiles("include/(voris/**.hpp)")
    add_includedirs("include", {public = true})
    add_files("src/*.cpp")
    add_defines("VORIS_VMEM_BUILD")

target_end()

if has_config("build_tests") then
    target("vmem_smoke_test")
        set_kind("binary")
        add_files("tests/smoke.cpp")
        add_deps("voris_vmem")
        add_tests("smoke")
    target_end()

    target("vmem_contracts_test")
        set_kind("binary")
        add_files("tests/contracts.cpp")
        add_deps("voris_vmem")
        add_tests("contracts")
    target_end()

    target("vmem_abi_contracts_test")
        set_kind("binary")
        add_files("tests/abi_contracts.cpp")
        add_deps("voris_vmem")
        add_tests("abi_contracts")
    target_end()

    target("vmem_public_headers_test")
        set_kind("binary")
        add_files("tests/public_headers/*.cpp")
        add_deps("voris_vmem")
        add_tests("public_headers")
    target_end()
end
