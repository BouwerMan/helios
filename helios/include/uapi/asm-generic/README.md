# Helios UAPI Defaults (`asm-generic/`)

`asm-generic/` contains **architecture-agnostic defaults** for the arch-specific
UAPI headers that live in `asm/`. An arch’s header may include one of these
files to inherit common constants, types, and conventions, and then override
only what differs.
