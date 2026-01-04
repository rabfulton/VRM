
// todo
// function to pack rectangles
// start_atlas();
// add font to atlas
// add png to atlas
// add white square to atlas
// stop_atlas();
// atlas pack();
// write_atlas + lookups
void load_font(const char *str, uint16_t font_size){

	uint32_t bitmap_size = 256 * 256;
	uint32_t ttf_buf_size = 1 << 20;

	uint8_t *ttf_buffer = malloc(ttf_buf_size);
	uint8_t *bitmap = malloc(bitmap_size);

	FILE fp = fopen(str, "r");
	if (fp == NULL){
		printf("ERROR: File not found : %s\n", str);
		goto cleanup;
	}
	if (!fread(ttf_buf, 1, ttf_buf_size, fp)){
		printf("ERROR: Unable to read %s\n", str);
		goto cleanup;
	}

	stbtt_bakedchar *glyphs = malloc(sizeof(stbtt_bakedchar) * 96);
	// need used rows in font[0]. struct
	// then next I can be appended
	used_rows = stbtt_BakeFontBitmap(ttf_buf, 0, font_size, bitmap, 256, 256, 32, 96, glyphs);

	load(texture();
cleanup:
	if (fp) fclose(fp);
	if (ttf_buf) free(ttf_buf);
	if (bitmap) free(bitmap);

	return glyphs;
}






