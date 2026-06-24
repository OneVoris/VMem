# Migration Notes

## v0.1.0

### Page Source Platforms

`os_page_source` now performs real page reservation on Windows and macOS. Code that previously expected `reserve`, `commit`, `decommit`, or `release` to return `errc::unsupported_platform` on those systems should switch to handling successful page spans.

Unknown platforms still return `errc::unsupported_platform`.

### Huge Pages

Huge pages are disabled by default. Existing calls keep ordinary page behavior:

```cpp
voris::mem::os_page_source pages;
auto span = pages.reserve(size);
```

To request huge pages opportunistically, use `page_source_options` and keep fallback enabled:

```cpp
voris::mem::page_source_options options{
    .prefer_huge_pages = true,
    .allow_huge_page_fallback = true,
};
auto span = pages.reserve(size, options);
```

Callers that require huge pages may set `allow_huge_page_fallback = false`, but must handle `errc::unsupported_platform`, `errc::out_of_memory`, and `errc::invalid_alignment`.

### Cache-Line Assumptions

Include `voris/mem/platform.hpp` when code needs VMem's cache-line assumption. `cache_line_size` is 64 only on x86_64 and arm64, with static checks for both branches. It is zero on unknown architectures, and callers must provide their own platform validation before using a cache-line layout assumption there. Runtime validation requires a target runner.
