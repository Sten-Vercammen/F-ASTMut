#include "mini.h"


Mini::Mini() {};

int Mini::g(int b) {
    this->a = b;
    return this->a;
}

int Mini::f(int b) {
    return g(3*b);
}

Maxi::Maxi() {};

int Maxi::f(int b) {
   return  Mini::f(2*b);
}

