#ifndef __A_FASTACTOR_H__
#define __A_FASTACTOR_H__

#include "actor.h"
#include "info.h"

class AFastActor : public AActor
{
	DECLARE_CLASS(AFastActor, AActor)
public:
	void Tick();
};

#endif //__A_FASTACTOR_H__
