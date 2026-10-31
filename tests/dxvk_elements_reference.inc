/* Exact classifier before numeric hot-path optimization. */
size_t elements(const Context &c,const char *method,size_t index) {
  if((strstr(method,"ShaderConstantF")||strstr(method,"ShaderConstantI"))&&index==1)return size_t(c.a[2])*4;
  if(strstr(method,"ShaderConstantB")&&index==1)return c.a[2];
  if((!strcmp(method,"SetClipPlane")||!strcmp(method,"GetClipPlane"))&&index==1)return 4;
  if((!strcmp(method,"SetPaletteEntries")||!strcmp(method,"GetPaletteEntries"))&&index==1)return 256;
  if(!strcmp(method,"Clear")&&index==1)return c.a[0];
  if(!strcmp(method,"GetDeclaration")&&index==0)return ref<UINT>(c.a[1]);
  return 1;
}
