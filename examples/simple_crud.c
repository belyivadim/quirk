#define QUIRK_IMPLEMENTATION
#include "../quirk.h"

#define STR(cstr) str_from_cstr(cstr)

typedef struct {
  int id;
  StringView title;
  StringView content;
  StringView tag;
  int created;
} Note;

sqlite3 *db;

void init_test_db(const QkStructMapping *mapping) {
  const char *db_path = ":memory:"; // In-memory DB
  // const char *db_path = "notes.db";
  sqlite3_open(db_path, &db);

  sqlite3_exec(db,
               "CREATE TABLE notes ("
               "id INTEGER PRIMARY KEY, "
               "title TEXT NOT NULL, "
               "content TEXT, "
               "tag TEXT, "
               "created INTEGER);",
               NULL, NULL, NULL);

  Note notes[4] = {
      {
          .title = sv_from_cstr("Pasta"),
          .content = sv_from_cstr("Use eggs, cheese, pancetta"),
          .tag = sv_from_cstr("#recipe"),
          .created = 1710200000,
      },
      {
          .title = sv_from_cstr("Meeting notes"),
          .content = sv_from_cstr("Discuss project"),
          .tag = sv_from_cstr("#work"),
          .created = 1710300000,
      },
      {
          .title = sv_from_cstr("Banana bread"),
          .content = sv_from_cstr("Use ripe bananas and walnuts."),
          .tag = sv_from_cstr("#recipe"),
          .created = 1710500000,
      },
      {
          .title = sv_from_cstr("Workout plan"),
          .content = sv_from_cstr("Leg dat and cardio"),
          .tag = sv_from_cstr("#fitness"),
          .created = 1710800000,
      },
  };

  QkParamRows params;
  da_alloc_reserved(params, 4);
  params.count = 4;
  StrArr columns = {0};
  qk_map_struct_to_cols_and_values(&notes[0], mapping, &columns,
                                   &params.items[0]);
  qk_map_struct_to_cols_and_values(&notes[1], mapping, NULL, &params.items[1]);
  qk_map_struct_to_cols_and_values(&notes[2], mapping, NULL, &params.items[2]);
  qk_map_struct_to_cols_and_values(&notes[3], mapping, NULL, &params.items[3]);

  QkSqlQuery q = qk_sql_insert_many(STR("notes"), columns, params);

  qk_sql_exec_sqlite(&q, db, NULL);

  qk_sql_query_free(&q);
}

bool exec_query_and_print_results(QkSqlQuery *q, QkStructMapping *m) {
  QkResultSet result = {0};
  if (!qk_sql_exec_sqlite(q, db, &result))
    return false;

  for (size_t i = 0; i < result.rows.count; ++i) {
    Note note = {0};
    qk_map_row_to_struct(&result.rows.items[i], m, &note);
    printf("id: %d\n", note.id);
    printf("title: '" sv_farg "'\n", sv_expand(note.title));
    printf("content: '" sv_farg "'\n", sv_expand(note.content));
    printf("tag: '" sv_farg "'\n", sv_expand(note.tag));
    printf("created: %d\n", note.created);
    printf("------\n");
  }

  qk_result_set_free(&result);

  return true;
}

#define CLEANUP                                                                \
  qk_sql_query_free(&q);                                                       \
  qk_struct_mapping_free(&note_mapping);                                       \
  sqlite3_close(db);                                                           \
  garena_free();

int main(void) {
  CG_ALLOCATOR_PUSH(std_allocator);

  QkStructMapping note_mapping = {
      .fields =
          da_from_list(QkStructField, QK_MAP_FIELD(Note, id, QK_INT, false),
                       QK_MAP_FIELD(Note, title, QK_STR, true),
                       QK_MAP_FIELD(Note, content, QK_STR, true),
                       QK_MAP_FIELD(Note, tag, QK_STR, true),
                       QK_MAP_FIELD(Note, created, QK_INT, true)),
      .string_mapping = QK_STR_TO_SV,
  };

  init_test_db(&note_mapping);

  QkSqlQuery q = qk_sql_select(STR("notes"), STR("*"));
  // qk_sql_where(&q, QK_FILT_LT, STR("id"), qk_int(3));
  if (!exec_query_and_print_results(&q, &note_mapping)) {
    CLEANUP;
    return 1;
  }
  qk_sql_query_free(&q);
  printf("====================================\n");

  q = qk_sql_delete(STR("notes"));
  qk_sql_where(&q, QK_FILT_EQ, STR("id"), qk_int(2));
  if (!exec_query_and_print_results(&q, &note_mapping)) {
    CLEANUP;
    return 1;
  }
  qk_sql_query_free(&q);

  q = qk_sql_update(STR("notes"), STR("title"), qk_cstr("Pasta Carbonara"));
  qk_sql_where(&q, QK_FILT_EQ, STR("id"), qk_int(1));
  if (!exec_query_and_print_results(&q, &note_mapping)) {
    CLEANUP;
    return 1;
  }
  qk_sql_query_free(&q);

  q = qk_sql_select(STR("notes"), STR("*"));
  if (!exec_query_and_print_results(&q, &note_mapping)) {
    CLEANUP;
    return 1;
  }
  qk_sql_query_free(&q);

  CLEANUP;
  return 0;
}
