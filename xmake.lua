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
        add_undefines("NDEBUG")
        add_tests("smoke")
    target_end()

    target("vmem_contracts_test")
        set_kind("binary")
        add_files("tests/contracts.cpp")
        add_deps("voris_vmem")
        add_undefines("NDEBUG")
        add_tests("contracts")
    target_end()

    target("vmem_abi_contracts_test")
        set_kind("binary")
        add_files("tests/abi_contracts.cpp")
        add_deps("voris_vmem")
        add_undefines("NDEBUG")
        add_tests("abi_contracts")
    target_end()

    target("vmem_m1_resources_test")
        set_kind("binary")
        add_files("tests/m1_resources.cpp")
        add_deps("voris_vmem")
        add_undefines("NDEBUG")
        add_tests("m1_resources")
    target_end()

    target("vmem_m2_resources_test")
        set_kind("binary")
        add_files("tests/m2_resources.cpp")
        add_deps("voris_vmem")
        add_undefines("NDEBUG")
        add_tests("m2_resources")
    target_end()

    target("vmem_m3_buffers_test")
        set_kind("binary")
        add_files("tests/m3_buffers.cpp")
        add_includedirs("src")
        add_deps("voris_vmem")
        add_undefines("NDEBUG")
        add_defines("VORIS_VMEM_ENABLE_TEST_HOOKS")
        add_tests("m3_buffers")
    target_end()

    target("vmem_public_headers_test")
        set_kind("binary")
        add_files("tests/public_headers/*.cpp")
        add_deps("voris_vmem")
        add_undefines("NDEBUG")
        add_tests("public_headers")
    target_end()
end

if has_config("build_benchmarks") then
    target("vmem_m2_resources_benchmark")
        set_kind("binary")
        add_files("benchmarks/m2_resources_benchmark.cpp")
        add_deps("voris_vmem")
    target_end()

    target("vmem_m3_buffers_benchmark")
        set_kind("binary")
        add_files("benchmarks/m3_buffers_benchmark.cpp")
        add_includedirs("src")
        add_deps("voris_vmem")
    target_end()
end

if has_config("build_fuzzers") then
    target("vmem_m3_buffer_chain_fuzz")
        set_kind("binary")
        add_files("fuzz/buffer_chain_fuzz.cpp")
        add_deps("voris_vmem")
    target_end()
end
