# The basics

* The ABI of a library is the set of its exported or _provided_
symbols.  For the purposes of this document, a symbols is _provided_
if it is eligible by `ld.so` to resolve an external reference.

* Each provided symbol is supplemented with the information about
its type.  This completes the notion of ABI used in this document.
Other aspects, such as semantics, are expressly excluded.

* Symbols along with their types are atomic; there is no further
structure in them.  Thus any change in the supplemented type yields
a new, different symbol (not unlike with C++ mangling).

Note that the supplemented type can describe its symbol more
or less rigorously.  For example, `const` qualifiers can be omitted
from the supplemented types.  This will yield "const-insensitive" ABI:
symbols will stay the same regardless of adding or removing `const`
qualifiers.

Atomic typing makes it possible to represent the ABI of a library
with a set-version, where each symbol is represented with its hash
value.  It is not difficult to imagine, on the other hand, the exact
representation where types remain amenable to structural comparison.
Thus adding `const` qualifier to an argument would keep the symbol
compatible, but removing would not.  This would also elevate the ABI
checker to arbiter the language issues.  In this document, however,
we seek to evade such a responsibility.  Instead, we devise a simpler
and somewhat lower-level typing scheme which can ensure consistency
of argument passing, along with some, but not all, expectations
between the caller and the callee.

# Primitive types

* Types are generally peeled off, as in `dwarf_peel_type`; `typedef`
aliases are followed and expanded; `const`, `volatile`, `restrict`,
and `atomic` qualifiers are removed.

* Integral types are further insensitive to the `unsigned` specifier.
N-byte integers are typed as `iN`.  Thus `bool` and `char` are typed
as `i1`; the type `short` is typed as `i2`, and so on.

* Floats are typed as `fN`.  On x86 and x86-64, 80-bit long double
is typed as `f10`.
