extern int GET();
extern void * MALLOC(int);
extern void FREE(void *);
extern void PRINT(int);

int main() {
   int a = 10;
   int b = 10;
   if (a == 10) {
     b = 20;
   } else {
     b = 0;
   }
   PRINT(a*b);
}

//200