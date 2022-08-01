#include <stdio.h>
#include <dlfcn.h>

extern "C" int generate_aap_metadata(const char* aapMetadataFullPath, const char* library, const char* entrypoint);

int main(int argc, const char** argv)
{
	if (argc < 2) {
		printf("USAGE: %s [aap_metadata.xml] [so_name] [entrypoint]\n", argv[0]);
		return 1;
	}
	const char *library = argc < 3 ? "libjuce_jni.so" : argv[2];
	const char *entrypoint = argc < 4 ? "GetJuceAAPFactory" : argv[3];
	generate_aap_metadata(argv[1], library, entrypoint);
	printf("Done.\n");
}
