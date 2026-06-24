set_project("VMem")
set_version("0.1.0")
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

option("build_sanitizer_probes")
    set_default(false)
    set_showmenu(true)
    set_description("Build explicit expected-failure sanitizer visibility probes")
option_end()

option("sanitize")
    set_default("none")
    set_showmenu(true)
    set_values("none", "address-undefined", "thread")
    set_description("Enable sanitizer flags for supported non-Windows toolchains")
option_end()

option("with_voris_dependencies")
    set_default(false)
    set_showmenu(true)
    set_description("Resolve released Voris dependencies through VXrepo")
option_end()

if has_config("with_voris_dependencies") then
    -- This repository has no internal upstream package.
end

local sanitize = get_config("sanitize")
if not is_plat("windows") then
    if sanitize == "address-undefined" then
        add_cxflags("-fsanitize=address,undefined", "-fno-omit-frame-pointer", {force = true})
        add_ldflags("-fsanitize=address,undefined", {force = true})
    elseif sanitize == "thread" then
        add_cxflags("-fsanitize=thread", "-fno-omit-frame-pointer", {force = true})
        add_ldflags("-fsanitize=thread", {force = true})
    end
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

    target("vmem_m4_budgets_test")
        set_kind("binary")
        add_files("tests/m4_budgets.cpp")
        add_deps("voris_vmem")
        add_undefines("NDEBUG")
        add_tests("m4_budgets")
    target_end()

    target("vmem_m5_debug_observability_test")
        set_kind("binary")
        add_files("tests/m5_debug_observability.cpp")
        add_deps("voris_vmem")
        add_undefines("NDEBUG")
        add_tests("m5_debug_observability")
    target_end()

    target("vmem_m5_debug_stress_test")
        set_kind("binary")
        add_files("tests/m5_debug_stress.cpp")
        add_deps("voris_vmem")
        add_undefines("NDEBUG")
        add_tests("m5_debug_stress")
    target_end()

    target("vmem_m6_platform_release_test")
        set_kind("binary")
        add_files("tests/m6_platform_release.cpp")
        add_deps("voris_vmem")
        add_undefines("NDEBUG")
        add_tests("m6_platform_release")
    target_end()

    target("vmem_m6_huge_registry_test")
        set_kind("binary")
        add_files("tests/m6_huge_registry.cpp")
        add_files("src/page_source.cpp")
        add_includedirs("include", "src")
        add_defines("VORIS_VMEM_ENABLE_PAGE_SOURCE_TEST_HOOKS", "VORIS_VMEM_BUILD")
        add_undefines("NDEBUG")
        add_tests("m6_huge_registry")
    target_end()

    target("vmem_m6_platform_assumption_x86_64_test")
        set_kind("binary")
        add_files("tests/public_headers/main.cpp")
        add_files("tests/m6_platform_assumption_x86_64.cpp")
        add_deps("voris_vmem")
        add_undefines("NDEBUG")
        add_tests("m6_platform_assumption_x86_64")
    target_end()

    target("vmem_m6_platform_assumption_arm64_test")
        set_kind("binary")
        add_files("tests/public_headers/main.cpp")
        add_files("tests/m6_platform_assumption_arm64.cpp")
        add_deps("voris_vmem")
        add_undefines("NDEBUG")
        add_tests("m6_platform_assumption_arm64")
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

    target("vmem_m4_budgets_benchmark")
        set_kind("binary")
        add_files("benchmarks/m4_budgets_benchmark.cpp")
        add_deps("voris_vmem")
    target_end()

    target("vmem_m5_debug_observability_benchmark")
        set_kind("binary")
        add_files("benchmarks/m5_debug_observability_benchmark.cpp")
        add_deps("voris_vmem")
    target_end()

    target("vmem_m6_release_benchmark")
        set_kind("binary")
        add_files("benchmarks/m6_release_benchmark.cpp")
        add_deps("voris_vmem")
    target_end()
end

if has_config("build_examples") then
    target("vmem_basic_usage_example")
        set_kind("binary")
        add_files("examples/basic_usage.cpp")
        add_deps("voris_vmem")
    target_end()
end

if has_config("build_fuzzers") then
    target("vmem_m3_buffer_chain_fuzz")
        set_kind("binary")
        add_files("fuzz/buffer_chain_fuzz.cpp")
        add_deps("voris_vmem")
    target_end()

    target("vmem_m5_allocator_corruption_fuzz")
        set_kind("binary")
        add_files("fuzz/allocator_corruption_fuzz.cpp")
        add_deps("voris_vmem")
    target_end()
end

if has_config("build_sanitizer_probes") then
    target("vmem_m5_asan_ubsan_visibility_probe")
        set_kind("binary")
        add_files("tests/m5_sanitizer_visibility.cpp")
        add_deps("voris_vmem")
        add_defines("VORIS_VMEM_M5_ASAN_UBSAN_VISIBILITY_PROBE")
    target_end()

    target("vmem_m5_tsan_visibility_probe")
        set_kind("binary")
        add_files("tests/m5_sanitizer_visibility.cpp")
        add_deps("voris_vmem")
        add_defines("VORIS_VMEM_M5_TSAN_VISIBILITY_PROBE")
    target_end()
end
