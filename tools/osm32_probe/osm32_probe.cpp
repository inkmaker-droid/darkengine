#include <osm_emu/osm32img.h>

#include <stdio.h>
#include <string>

int main(int argc, char **argv)
{
   if (argc < 2)
   {
      fprintf(stderr, "usage: osm32_probe module.osm [module.osm ...]\n");
      return 2;
   }

   int failures = 0;
   for (int i = 1; i < argc; ++i)
   {
      osm32::cImage image;
      std::string error;
      if (!image.Load(argv[i], &error))
      {
         fprintf(stderr, "%s: FAIL: %s\n", argv[i], error.c_str());
         ++failures;
         continue;
      }

      const uint32_t init = image.FindExport("_ScriptModuleInit@20");
      if (!init)
      {
         fprintf(stderr, "%s: FAIL: no _ScriptModuleInit@20 export\n", argv[i]);
         ++failures;
         continue;
      }

      if (!image.Relocate(image.PreferredBase() + 0x10000000u, &error))
      {
         fprintf(stderr, "%s: FAIL: relocation: %s\n", argv[i], error.c_str());
         ++failures;
         continue;
      }

      printf("%s: PE32 base=%08X size=%08X entry=%08X init=%08X imports=%u PASS\n",
             argv[i], image.PreferredBase(), image.ImageSize(), image.EntryPoint(),
             image.FindExport("_ScriptModuleInit@20"),
             static_cast<unsigned>(image.Imports().size()));
   }
   return failures ? 1 : 0;
}
