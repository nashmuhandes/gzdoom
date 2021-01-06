#ifndef __DMYCUSTOMCLASS_H__
#define __DMYCUSTOMCLASS_H__

#include <stdlib.h>
#include "dobject.h"

class DMyCustomClass : public DObject
{
	DECLARE_CLASS(DMyCustomClass, DObject)

public:
	void DoSomething(void);
	FString GetSomething(FString txt);
};

#endif //__DMYCUSTOMCLASS_H__
