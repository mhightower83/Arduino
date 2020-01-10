void processKey(Print& out, int hotKey) {
  switch (hotKey) {
    case 'r':
      out.printf("Reset, ESP.reset(); (%ld ms) ...\n", millis());
      ESP.reset();
      break;
    case 't':
      out.printf("Restart, ESP.restart(); (%ld ms) ...\n", millis());
      ESP.restart();
      break;
    case 's':
      out.printf("Crash with Software WDT (%ld ms) ...\n", millis());
      while (true) {
        // Wait for Software WDT to kick in.
      }
      break;
    case 'h':
      out.printf("Crash with Hardware WDT (%ld ms) ...\n", millis());
      // ESP.wdtDisable();
      asm volatile("" ::: "memory");
      asm volatile ("mov.n a2, %0\n"
                    "mov.n a3, %1\n"
                    "mov.n a4, %1\n"
                    "mov.n a5, %1\n"
                    "mov.n a6, %1\n"
                    : : "r" (0xaaaaaaaa), "r" (0xaaaaaaaa), "r" (0xaaaaaaaa), "r" (0xaaaaaaaa), "r" (0xaaaaaaaa) );
      // Could not find a stack save in the stack dumps, unless interrupts were enabled.
      while (true) {
        xt_rsil(15);
        // stay in an infinite loop doing nothing
        // this way other process can not be executed
        //
        // Note:
        // Hardware WDT kicks in if Software WDT is unable to perfrom.
        // With the Hardware WDT, nothing is saved on the stack, that I have seen.
      }
      break;

    case 'z':
      out.printf("Add a test (%ld ms) ...\n", millis());
      break;
    case 'm':
      {
        printDramMap(Serial);
        break;
      }
    case 'u':
      {
        umm_info(NULL, true);
        uint16_t hmax = 0;
        uint8_t  hfrag = 0;
        uint32_t hfree = 0;
        ESP.getHeapStats(&hfree, &hmax, &hfrag);
        out.printf("Heap Free Space:      %5u Bytes", hfree);
        out.println();
        out.printf("Heap Free Contiguous: %5u Bytes", hmax);
        out.println();
        out.printf("Heap Fragmentation:   %5u %%", hfrag);
        out.println();
      }
      break;
    case '\n':
    case '\r':
      break;
    default:
      out.printf("\"%c\" - Not an option?  / ? - help", hotKey);
      out.println();
    case '?':
      out.println();
      out.println("Press a key + <enter>");
      out.println("  r    - Reset, ESP.reset();");
      out.println("  t    - Restart, ESP.restart();");
      out.println("  u    - Print Heap Info, umm_info(NULL, true);");
      out.println("  m    - DRAM Memory Map");
      out.println("  z    - Test dejour");
      out.println("  ?    - Print Help");
      out.println();
      out.println("Crash with:");
      out.println("  s    - Software WDT");
      out.println("  h    - Hardware WDT");
      out.println();
      break;
  }
}
