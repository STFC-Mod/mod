// The C entry point Swift imports through module.modulemap. Returns librsync's rs_result: 0 (RS_DONE) on success.

int rsync_apply(const char* source_file, const char* patch_file, const char* target_file);
