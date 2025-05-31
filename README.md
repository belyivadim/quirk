# Quirk

**Quirk** is a lightweight, header-only C library for building type-safe SQL-like queries in C. It focuses on simple and expressive CRUD operations, with current support for `WHERE` and `ORDER BY` clauses.

This is part of a broader system aimed at giving C programs LINQ-style capabilities for SQL-backed datasets.

## Features

- Header-only, minimal dependencies
- Compile-time type safety for query parameters and result fields
- `SELECT`, `INSERT`, `UPDATE`, and `DELETE` support
- Composable `WHERE` and `ORDER_BY` clauses
- Simple and expressive API
- Interoperability with SQLite via `sqlite3_stmt`

## Limitations (Current Version)

- Only `WHERE` and `ORDER_BY` clauses are supported
- Limited clause composition and no support yet for `GROUP BY`, `HAVING`, `JOIN`, or subqueries

## Getting Started

### 1. Include the Library

Copy `quirk.h` into your project and include it:

```c
#define QUIRK_IMPLEMENTATION // to act as a C file
#include "quirk.h"
```

### 2. Define Your Schema

You can define your structs and describe them with `QkStructMapping`
More information about usage you can find in examples directory.

## Dependencies

- Requires SQLite (`sqlite3.h`) so link with `-lsqlite3`
- Uses one other my library called `cghost` (primarily for memory management and string operations): [`cghost repo`](https://github.com/belyivadim/cghost)

## Example

See [`simple_crud.c`](./examples/simple_crud.c) for a complete working example that:

- Initializes a database (not a part of the library)
- Creates a table (not a part of the library)
- Inserts rows
- Reads rows with filters
- Updates and deletes rows

## Roadmap

- Add support for raw queries that will just encapsulate parameters binding
- Add more SQL operations and clauses
- Code generation for schema introspection

## License

[`MIT License`](./LICENSE)
