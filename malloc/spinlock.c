#include "spinlock.h"


void spin_lock(slock* l) {
  asm volatile(".set mips2\n\t"
  "test_and_set:\n\t"
  "li $8, 1\n\t"
  "ll $9, 0($4)\n\t"
  "bnez $9, test_and_set\n\t"
  "sc $8, 0($4)\n\t"
  "beqz $8, test_and_set\n\t"
  );
}

/*mutex_lock(int *m) {
    test_and_set:
      LI $t0, 1
      LL $t1, 0($a0)
      BNEZ $t1, test_and_set
      SC $t0, 0($a0)
      BEQZ $t0, test_and_set
}*/
void spin_unlock(slock* l) {
  *l = 0;
}
