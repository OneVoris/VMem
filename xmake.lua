set_project("VMem")
set_version("0.1.0")
set_languages("c++23")
set_warnings("allextra")

add_rules("mode.debug", "mode.release")

function vmem_test_target(name, sourcefiles, options)
    options = options or {}

    target("vmem_" .. name .. "_test")
        set_kind("binary")
        set_default(false)
        set_group("tests")
        for _, sourcefile in ipairs(sourcefiles) do
            add_files(sourcefile)
        end
        for _, includedir in ipairs(options.includedirs or {}) do
            add_includedirs(includedir)
        end
        if options.deps ~= false then
            add_deps("voris_vmem")
        end
        add_undefines("NDEBUG")
        for _, define in ipairs(options.defines or {}) do
            add_defines(define)
        end
        add_tests(name)
    target_end()
end

target("voris_vmem")
    if get_config("kind") == "shared" then
        set_kind("shared")
        add_defines("VORIS_VMEM_SHARED", {public = true})
    else
        set_kind("static")
    end
    add_headerfiles("include/voris/**.hpp")
    add_includedirs("include", {public = true})
    add_files("src/*.cpp")
    add_defines("VORIS_VMEM_BUILD")
target_end()

vmem_test_target("smoke", {"tests/smoke.cpp"})
vmem_test_target("contracts", {"tests/contracts.cpp"})
vmem_test_target("abi_contracts", {"tests/abi_contracts.cpp"})
vmem_test_target("m1_resources", {"tests/m1_resources.cpp"})
vmem_test_target("m2_resources", {"tests/m2_resources.cpp"})
vmem_test_target("m3_buffers", {"tests/m3_buffers.cpp"}, {
    includedirs = {"src"},
    defines = {"VORIS_VMEM_ENABLE_TEST_HOOKS"}
})
vmem_test_target("m4_budgets", {"tests/m4_budgets.cpp"})
vmem_test_target("m5_debug_observability", {"tests/m5_debug_observability.cpp"})
vmem_test_target("m5_debug_stress", {"tests/m5_debug_stress.cpp"})
vmem_test_target("m6_platform_release", {"tests/m6_platform_release.cpp"})
vmem_test_target("m6_huge_registry", {"tests/m6_huge_registry.cpp", "src/page_source.cpp"}, {
    deps = false,
    includedirs = {"include", "src"},
    defines = {"VORIS_VMEM_ENABLE_PAGE_SOURCE_TEST_HOOKS", "VORIS_VMEM_BUILD"}
})
vmem_test_target("m6_platform_assumption_x86_64", {
    "tests/public_headers/main.cpp",
    "tests/m6_platform_assumption_x86_64.cpp"
})
vmem_test_target("m6_platform_assumption_arm64", {
    "tests/public_headers/main.cpp",
    "tests/m6_platform_assumption_arm64.cpp"
})
vmem_test_target("public_headers", {"tests/public_headers/*.cpp"})

target("vmem_m2_resources_benchmark")
    set_kind("binary")
    set_default(false)
    set_group("benchmarks")
    add_files("benchmarks/m2_resources_benchmark.cpp")
    add_deps("voris_vmem")
target_end()

target("vmem_m3_buffers_benchmark")
    set_kind("binary")
    set_default(false)
    set_group("benchmarks")
    add_files("benchmarks/m3_buffers_benchmark.cpp")
    add_includedirs("src")
    add_deps("voris_vmem")
target_end()

target("vmem_m4_budgets_benchmark")
    set_kind("binary")
    set_default(false)
    set_group("benchmarks")
    add_files("benchmarks/m4_budgets_benchmark.cpp")
    add_deps("voris_vmem")
target_end()

target("vmem_m5_debug_observability_benchmark")
    set_kind("binary")
    set_default(false)
    set_group("benchmarks")
    add_files("benchmarks/m5_debug_observability_benchmark.cpp")
    add_deps("voris_vmem")
target_end()

target("vmem_m6_release_benchmark")
    set_kind("binary")
    set_default(false)
    set_group("benchmarks")
    add_files("benchmarks/m6_release_benchmark.cpp")
    add_deps("voris_vmem")
target_end()

target("vmem_basic_usage_example")
    set_kind("binary")
    set_default(false)
    set_group("examples")
    add_files("examples/basic_usage.cpp")
    add_deps("voris_vmem")
target_end()

target("vmem_m3_buffer_chain_fuzz")
    set_kind("binary")
    set_default(false)
    set_group("fuzz")
    add_files("fuzz/buffer_chain_fuzz.cpp")
    add_deps("voris_vmem")
target_end()

target("vmem_m5_allocator_corruption_fuzz")
    set_kind("binary")
    set_default(false)
    set_group("fuzz")
    add_files("fuzz/allocator_corruption_fuzz.cpp")
    add_deps("voris_vmem")
target_end()

target("vmem_m5_asan_ubsan_visibility_probe")
    set_kind("binary")
    set_default(false)
    set_group("sanitizer-probes")
    add_files("tests/m5_sanitizer_visibility.cpp")
    add_deps("voris_vmem")
    add_defines("VORIS_VMEM_M5_ASAN_UBSAN_VISIBILITY_PROBE")
target_end()

target("vmem_m5_tsan_visibility_probe")
    set_kind("binary")
    set_default(false)
    set_group("sanitizer-probes")
    add_files("tests/m5_sanitizer_visibility.cpp")
    add_deps("voris_vmem")
    add_defines("VORIS_VMEM_M5_TSAN_VISIBILITY_PROBE")
target_end()
