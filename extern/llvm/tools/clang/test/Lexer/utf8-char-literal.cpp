// RUN: %clang_cc1 -triple x86_64-apple-darwin -std=c++11 -fsyntax-only -verify %s

int array0[u'�' == u'\xf1'? 1 : -1];
int array1['�' !=  u'\xf1'? 1 : -1];
