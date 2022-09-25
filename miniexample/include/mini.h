#ifndef mini_h
#define mini_h

#include <stdio.h>

class Mini {
    int a;
    int g(int b);

protected:
    int f(int b);

public:
    Mini();
};

class Maxi: Mini {

public:
    Maxi();
    int f(int b);
};


#endif /* mini_h */
