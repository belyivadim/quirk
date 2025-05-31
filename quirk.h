#ifndef __QUIRK_H__
#define __QUIRK_H__

#include <assert.h>
#include <sqlite3.h>
#include <stdio.h>

#include "cghost.h"

// === Type declaration ===
DA_DECL_TYPE(Str, StrArr)

typedef enum {
  QK_SQL_DIALECT_SQLITE,
} QkSqlDialect;

typedef enum {
  QK_SELECT,
  QK_UPDATE,
  QK_INSERT,
  QK_DELETE,
} QkOp;

typedef enum {
  QK_CONFLICT_NONE, // Default
  QK_CONFLICT_ROLLBACK,
  QK_CONFLICT_ABORT,
  QK_CONFLICT_FAIL,
  QK_CONFLICT_IGNORE,
  QK_CONFLICT_REPLACE,
} QkConflictResolution;

typedef enum {
  QK_FILT_NONE,
  QK_FILT_EQ,
  QK_FILT_NEQ,
  QK_FILT_GT,
  QK_FILT_LT,
  QK_FILT_LE,
  QK_FILT_GE,
  QK_FILT_MOD,
} QkFilter;

typedef enum {
  QK_ORDER_NONE,
  QK_ASC,
  QK_DESC,
} QkOrder;

typedef enum {
  QK_PARAM_NONE,
  QK_PARAM_NULL,
  QK_BOOL,
  QK_INT,
  QK_DOUBLE,
  QK_STR,
} QkParamKind;

typedef struct QkValue {
  QkParamKind kind;
  union {
    bool b;
    int i;
    double d;
    Str s;
  } as;
} QkParam;

DA_DECL_TYPE(QkParam, QkParamArr)
DA_DECL_TYPE(QkParamArr, QkParamRows)

#define qk_bool(b_)                                                            \
  (QkParam) { .kind = QK_BOOL, .as.b = (b_) }

#define qk_int(i_)                                                             \
  (QkParam) { .kind = QK_INT, .as.i = (i_) }

#define qk_double(d_)                                                          \
  (QkParam) { .kind = QK_DOUBLE, .as.d = (d_) }

#define qk_str(s_)                                                             \
  (QkParam) { .kind = QK_STR, .as.s = (s_) }

#define qk_cstr(s_) qk_str(str_from_cstr((s_)))

typedef struct QkColVal {
  Str column;
  QkParam param;
} QkColVal;

typedef struct QkSqlCond {
  QkColVal cv;
  QkFilter filt;
} QkSqlCond;

DA_DECL_TYPE(QkSqlCond, QkSqlCondArr)

typedef struct QkSqlQuery {
  QkOp op;
  QkConflictResolution conflic;

  Str table;

  // used for select, update
  StrArr columns;

  // used for select, update, delete
  QkSqlCondArr where;

  // used for insert, update
  QkParamRows param_rows;

  struct {
    Str column;
    QkOrder order;
  } order_by;

  int limit; // negative value means no limit

  StringBuilder b;
} QkSqlQuery;

typedef struct {
  Str column_name;
  QkParam value;
} QkResultColumn;

DA_STRUCT(QkResultColumn, QkResultColumnArr)

typedef struct {
  QkResultColumnArr columns;
} QkResultRow;

DA_STRUCT(QkResultRow, QkResultRowArr)

typedef struct {
  QkResultRowArr rows;
} QkResultSet;

typedef struct {
  StringView column_name;
  size_t offset;
  QkParamKind kind;
  bool map_from_struct;
} QkStructField;

#define QK_MAP_FIELD(s, f, qtype, map_from_struct_)                            \
  ((QkStructField){.column_name = sv_from_cstr(#f),                            \
                   .offset = offsetof(s, f),                                   \
                   .kind = (qtype),                                            \
                   .map_from_struct = (map_from_struct_)})

DA_DECL_TYPE(QkStructField, QkStructFieldArr)

typedef enum {
  QK_STR_TO_STR,
  QK_STR_TO_SV,
  QK_STR_TO_SB,
  QK_STR_TO_CSTR,
} QkStringMapping;

typedef struct {
  QkStructFieldArr fields;
  QkStringMapping string_mapping;
} QkStructMapping;

// === Function declarations ===

// NOTE: all Str that passed to the functions are "moved" (refcounter is not
// incremented, so if you need you have to clone it yourself before passing).
// You can still use them as long as you do not call qk_sql_query_free, but do
// not free these Str by yourself. However during mapping Str are cloned
// (refcounter is incremented)

QkSqlQuery qk_sql_select(Str table, Str column);
QkSqlQuery qk_sql_select_many(Str table, StrArr columns);
QkSqlQuery qk_sql_update(Str table, Str column, QkParam param);
QkSqlQuery qk_sql_update_many(Str table, StrArr columns, QkParamArr params);
QkSqlQuery qk_sql_insert(Str table, StrArr columns, QkParamArr params);
QkSqlQuery qk_sql_insert_many(Str table, StrArr columns,
                              QkParamRows param_rows);
QkSqlQuery qk_sql_delete(Str table);
void qk_sql_conflic_resolution(QkSqlQuery *q, QkConflictResolution conflic);
void qk_sql_where(QkSqlQuery *q, QkFilter filt, Str column, QkParam param);
void qk_sql_order_by(QkSqlQuery *q, Str column, QkOrder order);
void qk_sql_limit(QkSqlQuery *q, int limit);
bool qk_sql_build(QkSqlQuery *q, QkSqlDialect dialect);
bool qk_bind_param_sqlite(sqlite3_stmt *stmt, int idx, QkParam *p);
bool qk_sql_exec_sqlite(QkSqlQuery *q, sqlite3 *db, QkResultSet *out);
void qk_sql_query_free(QkSqlQuery *q);
void qk_result_set_free(QkResultSet *res);
void qk_struct_mapping_free(QkStructMapping *m);
void qk_map_row_to_struct(QkResultRow *row, const QkStructMapping *mapping,
                          void *struct_ptr);
void qk_map_struct_to_cols_and_values(const void *struct_ptr,
                                      const QkStructMapping *mapping,
                                      StrArr *out_columns,
                                      QkParamArr *out_values);
#endif // __QUIRK_H__

#ifdef QUIRK_IMPLEMENTATION

#define CGHOST_IMPLEMENTATION
#include "cghost.h"

// === Function definitions ===
QkSqlQuery qk_sql_select(Str table, Str column) {
  return (QkSqlQuery){
      .op = QK_SELECT,
      .table = table,
      .columns = da_from_list(Str, column),
      .limit = -1,
  };
}

QkSqlQuery qk_sql_select_many(Str table, StrArr columns) {
  return (QkSqlQuery){
      .op = QK_SELECT,
      .table = table,
      .columns = columns,
      .limit = -1,
  };
}

QkSqlQuery qk_sql_update(Str table, Str column, QkParam param) {
  return (QkSqlQuery){
      .op = QK_UPDATE,
      .table = table,
      .columns = da_from_list(Str, column),
      .param_rows = da_from_list(QkParamArr, da_from_list(QkParam, param)),
      .limit = -1,
  };
}

QkSqlQuery qk_sql_update_many(Str table, StrArr columns, QkParamArr params) {
  return (QkSqlQuery){
      .op = QK_UPDATE,
      .table = table,
      .columns = columns,
      .param_rows = da_from_list(QkParamArr, params),
      .limit = -1,
  };
}

QkSqlQuery qk_sql_insert(Str table, StrArr columns, QkParamArr params) {
  return (QkSqlQuery){
      .op = QK_INSERT,
      .table = table,
      .columns = columns,
      .param_rows = da_from_list(QkParamArr, params),
      .limit = -1,
  };
}

QkSqlQuery qk_sql_insert_many(Str table, StrArr columns,
                              QkParamRows param_rows) {
  return (QkSqlQuery){
      .op = QK_INSERT,
      .table = table,
      .columns = columns,
      .param_rows = param_rows,
      .limit = -1,
  };
}

QkSqlQuery qk_sql_delete(Str table) {
  return (QkSqlQuery){
      .op = QK_DELETE,
      .table = table,
      .limit = -1,
  };
}

void qk_sql_conflic_resolution(QkSqlQuery *q, QkConflictResolution conflic) {
  assert(q->conflic == QK_CONFLICT_NONE);
  q->conflic = conflic;
}

void qk_sql_where(QkSqlQuery *q, QkFilter filt, Str column, QkParam param) {
  QkSqlCond c =
      (QkSqlCond){.cv = {.column = column, .param = param}, .filt = filt};
  da_push(q->where, c);
}

void qk_sql_order_by(QkSqlQuery *q, Str column, QkOrder order) {
  assert(q->order_by.order == QK_ORDER_NONE);
  assert(column.h->b.count > 0);
  assert(order != QK_ORDER_NONE);
  q->order_by.column = column;
  q->order_by.order = order;
}

void qk_sql_limit(QkSqlQuery *q, int limit) {
  assert(q->limit == -1);
  q->limit = limit;
}

static void qk_sql_add_conflic_resolution(QkSqlQuery *q, QkSqlDialect dialect) {
  (void)dialect;
  switch (q->conflic) {
  case QK_CONFLICT_NONE:
    return;
  case QK_CONFLICT_ROLLBACK:
    sb_append_cstr(&q->b, "OR ROLLBACK ");
    return;
  case QK_CONFLICT_ABORT:
    sb_append_cstr(&q->b, "OR ABORT ");
    return;
  case QK_CONFLICT_FAIL:
    sb_append_cstr(&q->b, "OR FAIL ");
    return;
  case QK_CONFLICT_IGNORE:
    sb_append_cstr(&q->b, "OR IGNORE ");
    return;
  case QK_CONFLICT_REPLACE:
    sb_append_cstr(&q->b, "OR REPLACE ");
    return;
  }
}

bool qk_sql_build(QkSqlQuery *q, QkSqlDialect dialect) {
  q->b.count = 0;

  switch (q->op) {
  case QK_SELECT: {
    sb_append_cstr(&q->b, "SELECT ");
    qk_sql_add_conflic_resolution(q, dialect);
    for (size_t i = 0; i < q->columns.count; i += 1) {
      if (i > 0)
        sb_append_cstr(&q->b, ", ");
      sb_append_string_view(&q->b, &sv_from_str(q->columns.items[i]));
    }
    sb_append_cstr(&q->b, " FROM ");
    sb_append_string_view(&q->b, &sv_from_str(q->table));
  } break;

  case QK_UPDATE: {
    sb_append_cstr(&q->b, "UPDATE ");
    qk_sql_add_conflic_resolution(q, dialect);
    sb_append_str(&q->b, &q->table);
    sb_append_cstr(&q->b, " SET ");
    if (q->param_rows.count != 1 ||
        q->columns.count != q->param_rows.items[0].count)
      return false;
    for (size_t i = 0; i < q->columns.count; i += 1) {
      if (i > 0)
        sb_append_cstr(&q->b, ", ");
      sb_appendf(&q->b, "%.*s = ?", str_expand(q->columns.items[0]));
    }
  } break;

  case QK_INSERT: {
    sb_append_cstr(&q->b, "INSERT ");
    qk_sql_add_conflic_resolution(q, dialect);
    sb_append_cstr(&q->b, "INTO ");
    sb_append_str(&q->b, &q->table);
    sb_append_cstr(&q->b, " (");
    for (size_t i = 0; i < q->columns.count; i += 1) {
      if (i > 0) {
        sb_append_cstr(&q->b, ", ");
      }
      sb_append_str(&q->b, &q->columns.items[i]);
    }
    sb_append_cstr(&q->b, ") VALUES ");
    if (q->param_rows.count == 0)
      return false;
    size_t cols = q->param_rows.items[0].count;
    for (size_t i = 0; i < q->param_rows.count; i += 1) {
      if (i > 0) {
        sb_append_cstr(&q->b, ", ");
      }
      sb_append_rune(&q->b, '(');
      for (size_t j = 0; j < q->param_rows.items[i].count; j += 1) {
        if (cols != q->param_rows.items[i].count)
          return false;
        if (j > 0) {
          sb_append_cstr(&q->b, ", ");
        }
        sb_append_rune(&q->b, '?');
      }
      sb_append_rune(&q->b, ')');
    }
  } break;
  case QK_DELETE:
    sb_append_cstr(&q->b, "DELETE");
    qk_sql_add_conflic_resolution(q, dialect);
    sb_appendf(&q->b, "FROM %.*s", str_expand(q->table));
    break;
  }

  if (q->where.count > 0) {
    sb_append_cstr(&q->b, " WHERE ");
    for (size_t i = 0; i < q->where.count; i += 1) {
      QkSqlCond *cond = &q->where.items[i];
      if (i > 0)
        sb_append_cstr(&q->b, " AND ");
      sb_append_string_view(&q->b, &sv_from_str(cond->cv.column));

      switch (cond->filt) {
      case QK_FILT_NONE:
        fprintf(stderr,
                "[Warning] QK_FILT_NONE should not be passed to where clause");
        break;
      case QK_FILT_EQ:
        sb_append_cstr(&q->b, " = ?");
        break;
      case QK_FILT_NEQ:
        sb_append_cstr(&q->b, " != ?");
        break;
      case QK_FILT_GT:
        sb_append_cstr(&q->b, " > ?");
        break;
      case QK_FILT_LT:
        sb_append_cstr(&q->b, " < ?");
        break;
      case QK_FILT_LE:
        sb_append_cstr(&q->b, " <= ?");
        break;
      case QK_FILT_GE:
        sb_append_cstr(&q->b, " >= ?");
        break;
      case QK_FILT_MOD:
        sb_append_cstr(&q->b, " % ?");
        break;
      }
    }
  }

  if (q->order_by.order != QK_ORDER_NONE) {
    sb_append_cstr(&q->b, " ORDER BY ");
    sb_append_string_view(&q->b, &sv_from_str(q->order_by.column));
    if (q->order_by.order == QK_ASC)
      sb_append_cstr(&q->b, " ASC");
    else
      sb_append_cstr(&q->b, " DESC");
  }

  if (q->limit >= 0)
    sb_appendf(&q->b, " LIMIT %d", q->limit);

  sb_append_rune(&q->b, '\0');
  return true;
}

bool qk_bind_param_sqlite(sqlite3_stmt *stmt, int idx, QkParam *p) {
  switch (p->kind) {
  case QK_PARAM_NONE:
    fprintf(stderr, "[Error] QK_PARAM_NONE should not be passed to query\n");
    return false;
  case QK_PARAM_NULL:
    sqlite3_bind_null(stmt, idx);
    break;
  case QK_BOOL:
  case QK_INT:
    sqlite3_bind_int(stmt, idx, p->as.i);
    break;
  case QK_DOUBLE:
    sqlite3_bind_double(stmt, idx, p->as.d);
    break;
  case QK_STR:
    sqlite3_bind_text(stmt, idx, p->as.s.h->b.items, (int)p->as.s.h->b.count,
                      SQLITE_TRANSIENT);
    break;
  }

  return true;
}

static size_t qk_bind_params(QkSqlQuery *q, sqlite3_stmt *stmt, size_t offset) {
  size_t rows = q->param_rows.count;
  size_t cols = q->param_rows.items[0].count;

  for (size_t i = 0; i < rows; i += 1) {
    for (size_t j = 0; j < cols; j += 1) {
      size_t idx = offset + 1 + (i * cols + j);
      qk_bind_param_sqlite(stmt, idx, &q->param_rows.items[i].items[j]);
    }
  }

  return rows * cols;
}

static size_t qk_bind_where(QkSqlQuery *q, sqlite3_stmt *stmt, size_t offset) {
  for (int i = 0; i < (int)q->where.count; ++i) {
    qk_bind_param_sqlite(stmt, offset + i + 1, &q->where.items[i].cv.param);
  }
  return q->where.count;
}

bool qk_sql_exec_sqlite(QkSqlQuery *q, sqlite3 *db, QkResultSet *out) {
  if (!qk_sql_build(q, QK_SQL_DIALECT_SQLITE))
    return false;

  const char *sql = sb_get_cstr(&q->b);
  printf("Executing SQL: %s\n", sql);

  sqlite3_stmt *stmt = NULL;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
    fprintf(stderr, "SQL prepare error: %s\n", sqlite3_errmsg(db));
    return false;
  }

  switch (q->op) {
  case QK_DELETE:
  case QK_SELECT:
    qk_bind_where(q, stmt, 0);
    break;

  case QK_INSERT:
    qk_bind_params(q, stmt, 0);
    break;

  case QK_UPDATE: {
    size_t offset = qk_bind_params(q, stmt, 0);
    qk_bind_where(q, stmt, offset);
  } break;
  }

  // Execute and collect rows
  while (true) {
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE)
      break;
    else if (rc != SQLITE_ROW) {
      printf("[Error] sqlite3 step failed: %s\n", sqlite3_errmsg(db));
      break;
    }

    QkResultRow row = {0};
    int col_count = sqlite3_column_count(stmt);

    for (int i = 0; i < col_count; ++i) {
      Str col_name = str_from_cstr(sqlite3_column_name(stmt, i));
      int type = sqlite3_column_type(stmt, i);

      QkResultColumn col = {
          .column_name = col_name,
      };

      switch (type) {
      case SQLITE_INTEGER:
        col.value = qk_int(sqlite3_column_int(stmt, i));
        break;
      case SQLITE_FLOAT:
        col.value = qk_double(sqlite3_column_double(stmt, i));
        break;
      case SQLITE_TEXT: {
        Str text = str_from_cstr((const char *)sqlite3_column_text(stmt, i));
        col.value = qk_str(text);
        break;
      }
      case SQLITE_NULL:
        col.value.kind = QK_PARAM_NULL;
        break;
      default:
        fprintf(stderr, "Unsupported SQLite column type\n");
        break;
      }

      if (out != NULL) {
        da_push(row.columns, col);
      }
    }
    if (out != NULL) {
      da_push(out->rows, row);
    }
  }

  sqlite3_finalize(stmt);
  return true;
}

void qk_sql_query_free(QkSqlQuery *q) {
  if (NULL == q)
    return;

  // table
  str_free(&q->table);

  // columns
  for (size_t i = 0; i < q->columns.count; i += 1) {
    str_free(&q->columns.items[i]);
  }
  da_free(q->columns);

  // where
  for (size_t i = 0; i < q->where.count; i += 1) {
    QkSqlCond *cond = &q->where.items[i];
    str_free(&cond->cv.column);
    if (cond->cv.param.kind == QK_STR) {
      str_free(&cond->cv.param.as.s);
    }
  }
  da_free(q->where);

  // params
  for (size_t i = 0; i < q->param_rows.count; i += 1) {
    for (size_t j = 0; j < q->param_rows.items[i].count; j += 1) {
      QkParam *p = &q->param_rows.items[i].items[j];
      if (p->kind == QK_STR) {
        str_free(&p->as.s);
      }
    }
    da_free(q->param_rows.items[i]);
  }
  da_free(q->param_rows);

  // order by
  str_free(&q->order_by.column);

  // builder
  sb_free(q->b);

  memset(q, 0, sizeof(*q));
}

void qk_result_set_free(QkResultSet *res) {
  if (NULL == res)
    return;

  for (size_t i = 0; i < res->rows.count; i += 1) {
    for (size_t j = 0; j < res->rows.items[i].columns.count; j += 1) {
      QkResultColumn *col = &res->rows.items[i].columns.items[j];
      str_free(&col->column_name);
      if (col->value.kind == QK_STR) {
        str_free(&col->value.as.s);
      }
    }
    da_free(res->rows.items[i].columns);
  }
  da_free(res->rows);

  memset(res, 0, sizeof(*res));
}

void qk_map_row_to_struct(QkResultRow *row, const QkStructMapping *mapping,
                          void *struct_ptr) {
  for (size_t i = 0; i < mapping->fields.count; i += 1) {
    QkStructField *field = &mapping->fields.items[i];
    for (size_t j = 0; j < row->columns.count; j += 1) {
      QkResultColumn *col = &row->columns.items[j];
      if (col->value.kind == QK_PARAM_NULL || col->value.kind == QK_PARAM_NONE)
        continue;
      if (sv_equals_icase(&sv_from_str(col->column_name),
                          &field->column_name)) {
        void *field_ptr = (char *)struct_ptr + field->offset;
        switch (field->kind) {
        case QK_BOOL:
          *(bool *)field_ptr = col->value.as.b;
          break;
        case QK_INT:
          *(int *)field_ptr = col->value.as.i;
          break;
        case QK_DOUBLE:
          *(double *)field_ptr = col->value.as.d;
          break;
        case QK_STR: {
          switch (mapping->string_mapping) {
          case QK_STR_TO_STR:
            *(Str *)field_ptr = str_clone(&col->value.as.s);
            break;
          case QK_STR_TO_SV:
            *(StringView *)field_ptr = sv_from_str(col->value.as.s);
            break;
          case QK_STR_TO_SB:
            *(StringBuilder *)field_ptr = sb_clone(&col->value.as.s.h->b);
            break;
          case QK_STR_TO_CSTR: {
            size_t str_len = col->value.as.s.h->b.count;
            char *cstr = CG_MALLOC(CG_ALLOCATOR_INSTANCE, str_len);
            memcpy(cstr, col->value.as.s.h->b.items, str_len);
            *(char **)field_ptr = cstr;
          } break;
          }
        } break;
        case QK_PARAM_NONE:
        case QK_PARAM_NULL:
          break;
        }
      }
    }
  }
}

void qk_map_struct_to_cols_and_values(const void *struct_ptr,
                                      const QkStructMapping *mapping,
                                      StrArr *out_columns,
                                      QkParamArr *out_values) {
  for (size_t i = 0; i < mapping->fields.count; ++i) {
    QkStructField *field = &mapping->fields.items[i];
    if (!field->map_from_struct)
      continue;

    void *field_ptr = (char *)struct_ptr + field->offset;

    if (NULL != out_columns) {
      da_push(*out_columns, str_from_sv(field->column_name));
    }

    QkParam param = {0};

    switch (field->kind) {
    case QK_BOOL:
      param = qk_bool(*(bool *)field_ptr);
      break;
    case QK_INT:
      param = qk_int(*(int *)field_ptr);
      break;
    case QK_DOUBLE:
      param = qk_double(*(double *)field_ptr);
      break;
    case QK_STR:
      switch (mapping->string_mapping) {
      case QK_STR_TO_STR:
        param = qk_str(*(Str *)field_ptr);
        break;
      case QK_STR_TO_SV:
        param = qk_str(str_from_sv((*(StringView *)field_ptr)));
        break;
      case QK_STR_TO_SB:
        param = qk_str(str_from_sb(*(StringBuilder *)field_ptr));
        break;
      case QK_STR_TO_CSTR: {
        const char *cstr = *(char **)field_ptr;
        param = qk_str(str_from_cstr(cstr));
        break;
      }
      }
      break;
    case QK_PARAM_NONE:
    case QK_PARAM_NULL:
      param.kind = QK_PARAM_NONE;
      break;
    }

    da_push(*out_values, param);
  }
}

void qk_struct_mapping_free(QkStructMapping *m) {
  da_free(m->fields);
  memset(m, 0, sizeof(*m));
}

#endif // QUIRK_IMPLEMENTATION
