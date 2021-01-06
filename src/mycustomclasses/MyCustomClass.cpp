#include "vm.h"
#include "vmintern.h"
#include "MyCustomClass.h"

IMPLEMENT_CLASS(DMyCustomClass, false, false);

void DMyCustomClass::DoSomething(void)
{
}

DEFINE_ACTION_FUNCTION_NATIVE(DMyCustomClass, DoSomething)
{
	PARAM_SELF_STRUCT_PROLOGUE(DMyCustomClass);
	self->DoSomething();
	return 0;
}

FString DMyCustomClass::GetSomething(FString txt)
{
	return txt;
}

DEFINE_ACTION_FUNCTION_NATIVE(DMyCustomClass, GetSomething)
{
	PARAM_SELF_STRUCT_PROLOGUE(DMyCustomClass);
	PARAM_STRING(txt);
	ACTION_RETURN_STRING(self->GetSomething(txt));
}
