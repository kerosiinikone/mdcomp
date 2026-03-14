#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define FORMAT_VERSION "%PDF-2.0\n%µ¶\n\n"

int main() 
{
	char content_string[512];
	char user_content[] = "Hello Note";
	sprintf(content_string, "BT\n  /F1 18 Tf\n  50 700 Td\n  (%s) Tj\nET", user_content);

	uint64_t bytes_written = 0;

	FILE *fptr = fopen("output.pdf", "wb");
	if (!fptr) {
		return -1;
	}

	char header[64];
	sprintf(header, "%s", FORMAT_VERSION);
	bytes_written += fwrite(&header, sizeof(char), strlen(header), fptr);

	char root[] = "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n\n";
	bytes_written += fwrite(&root, sizeof(char), strlen(root), fptr);

	char tree[] = "2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n\n";
	bytes_written += fwrite(&tree, sizeof(char), strlen(tree), fptr);

	char page[] = "3 0 obj\n<<\n  /Type /Page\n  /Parent 2 0 R\n  /MediaBox [0 0 612 792]\n  /Resources << /Font << /F1 4 0 R >> >>\n  /Contents 5 0 R\n>>\nendobj\n\n";
	bytes_written += fwrite(&page, sizeof(char), strlen(page), fptr);

	char fonts_res[] = "4 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>\nendobj\n\n";
	bytes_written += fwrite(&fonts_res, sizeof(char), strlen(fonts_res), fptr);

	char contents[1024];
	sprintf(contents, "5 0 obj\n<< /Length %ld >>\nstream\n%s\nendstream\nendobj\n\n", strlen(content_string), content_string);
	bytes_written += fwrite(contents, sizeof(char), strlen(contents), fptr);

	char xref[] = "xref\n0 6\n0000000000 65535 f\n0000000015 00000 n\n0000000064 00000 n\n0000000122 00000 n\n0000000262 00000 n\n0000000334 00000 n\n";
	fwrite(&xref, sizeof(char), strlen(xref), fptr);

	char trailer[512];
	sprintf(trailer, "trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n%llu\n", (unsigned long long)bytes_written);
	fwrite(trailer, sizeof(char), strlen(xref), fptr);
	fputs("%%EOF\n", fptr);

	fclose(fptr);
	return 0;
}
