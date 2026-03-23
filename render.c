#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>

#define FORMAT_VERSION "%PDF-2.0\n%\xE2\xE3\xCF\xD3\n\n"

typedef enum {
	PDF_CATALOG,
	PDF_TREE,
	PDF_PAGE,
	PDF_CONTENT,
	PDF_FONT
} PDF_Object_Type;

typedef struct {
	int id;
	PDF_Object_Type type;

	union {
		// doc catalog
		// 1 0 obj 
		// << 
		// /Pages 2 0 R
		// /Type /Catalog
		// >> 
		// endobj
		struct {
			int pages_id;
		} catalog;
		// page tree
		// 2 0 obj
		// << 
		// /Count (page_count)
		// /Kids [pointer_to_pages] (3 0 R)
		// /Type /Pages
		// <<
		// endobj
		struct {
			int count;
			int *kids;
		} tree;
		// individual pages
		// 3 0 obj
		// <<
		// /Contents (pointer to contents) 4 0 R
		// /Mediabox (page size)
		// /Parent (pointer to tree)
		// /Resources <<
		// 	/Font << ... >> 
		// >>
		// /Type /Page
		struct {
			int contents_id;
			int mb_x;
			int mb_y;
			int parent_id;
			int font_id;
		} page;
		// font
		// 5 0 obj
		// <<
		// 	/BaseFont /Helvetica
		// 	/Encoding ...
		// 	/Subtype ...
		// 	/Type /Font
		// >>
		// endobj
		struct {
			size_t length;
			char *stream;
		} content;
		// page content objects (as pointed to in the pages objects)
		// 4 0 obj
		// <<
		// 	/Length (stream len)
		// >>
		// stream
		// [contents produced!]
		// endstream
		// endobj
	};
} PDF_Object;

typedef struct {
	FILE *f;
	long offsets[256];
	size_t obj_count;
} PDF_Context;

bool pdf_init(PDF_Context *ctx, const char *fp) {
	FILE *f = fopen(fp, "wb");
	if (f == NULL) return false;

	ctx->f = f;
	ctx->offsets[0] = 0;
	ctx->obj_count = 1;

	fprintf(f, "%s", FORMAT_VERSION);
	return true;
}


void pdf_obj_start(PDF_Context *pctx, PDF_Object *obj) {
	pctx->offsets[obj->id] = ftell(pctx->f);
	pctx->obj_count++;
	fprintf(pctx->f, "%d 0 obj\n", obj->id);
}

void pdf_obj_end(PDF_Context *pctx) {
	fprintf(pctx->f, "endobj\n\n");
}

void pdf_obj_write(PDF_Context *pctx, PDF_Object *obj) {
	pdf_obj_start(pctx, obj);
	switch (obj->type) {
		case PDF_CATALOG: {
			fprintf(pctx->f, "<< /Pages %d 0 R /Type /Catalog >>\n", obj->catalog.pages_id);
			break;
		}
		case PDF_TREE: {
			// TODO: dynamic table, count
			fprintf(pctx->f, "<< /Count %d /Kids [%d 0 R] /Type /Pages >>\n", obj->tree.count, obj->tree.kids[0]);
			break;
		}
		case PDF_PAGE: {
			fprintf(pctx->f, "<<\n");
			fprintf(pctx->f, "	/Parent %d 0 R\n", obj->page.parent_id);
			fprintf(pctx->f, "	/Contents %d 0 R\n", obj->page.contents_id);
			fprintf(pctx->f, "	/Mediabox [0 0 %d %d]\n", obj->page.mb_x, obj->page.mb_y);
			fprintf(pctx->f, "	/Resources << /Font << /F1 %d 0 R >> >>\n", obj->page.font_id);
			fprintf(pctx->f, ">>\n");
			fprintf(pctx->f, "/Type /Page\n");
			break;
		}
		case PDF_FONT: {
			// TODO: configure
			fprintf(pctx->f, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>\n");
			break;
		}
		case PDF_CONTENT: {
			fprintf(pctx->f, "<< /Length %zu >>\n", obj->content.length);
			fprintf(pctx->f, "stream\n");
			fprintf(pctx->f, "%s\n", obj->content.stream);
			fprintf(pctx->f, "endstream\n");
			break;
		}
		default:
		break;
	}
	pdf_obj_end(pctx);
}

void pdf_xref_table_write(PDF_Context *pctx) {
	fprintf(pctx->f, "xref\n");
	fprintf(pctx->f, "0 %zu\n", pctx->obj_count);
	fprintf(pctx->f, "0000000000 65535 f \n");
	for (int i = 1; i < pctx->obj_count; i++) {
		fprintf(pctx->f, "%010ld 00000 n \n", pctx->offsets[i]);
	}
}

void pdf_trailer_write(PDF_Context *pctx, int root) {
	size_t startxref = ftell(pctx->f);
	fprintf(pctx->f, "trailer\n");
	fprintf(pctx->f, "<< /Size %zu /Root %d 0 R >>\n", pctx->obj_count, root);
	fprintf(pctx->f, "startxref\n%ld\n%%%%EOF\n", startxref);
} 
