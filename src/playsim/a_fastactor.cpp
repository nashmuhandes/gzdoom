#include <float.h>
#include "templates.h"

#include "m_random.h"
#include "doomdef.h"
#include "p_local.h"
#include "p_maputl.h"
#include "p_lnspec.h"
#include "p_effect.h"
#include "p_terrain.h"
#include "hu_stuff.h"
#include "v_video.h"
#include "c_dispatch.h"
#include "b_bot.h"	//Added by MC:
#include "a_sharedglobal.h"
#include "gi.h"
#include "sbar.h"
#include "p_acs.h"
#include "cmdlib.h"
#include "decallib.h"
#include "a_keys.h"
#include "p_conversation.h"
#include "g_game.h"
#include "teaminfo.h"
#include "r_sky.h"
#include "d_event.h"
#include "p_enemy.h"
#include "gstrings.h"
#include "po_man.h"
#include "p_spec.h"
#include "p_checkposition.h"
#include "serializer_doom.h"
#include "serialize_obj.h"
#include "r_utility.h"
#include "thingdef.h"
#include "d_player.h"
#include "g_levellocals.h"
#include "a_morph.h"
#include "events.h"
#include "actorinlines.h"
#include "a_dynlight.h"
#include "fragglescript/t_fs.h"

#include "a_fastactor.h"

double P_XYMovement(AActor* mo, DVector2 scroll);
void P_ZMovement(AActor* mo, double oldfloorz);

IMPLEMENT_CLASS(AFastActor, false, false)

void AFastActor::Tick()
{
	AActor* onmo;

	//assert (state != NULL);
	if (state == NULL)
	{
		Printf("Actor of type %s at (%f,%f) left without a state\n", GetClass()->TypeName.GetChars(), X(), Y());
		Destroy();
		return;
	}

	if (flags5 & MF5_NOINTERACTION)
	{
		// only do the minimally necessary things here to save time:
		// Check the time freezer
		// apply velocity
		// ensure that the actor is not linked into the blockmap

		//Added by MC: Freeze mode.
		if (isFrozen())
		{
			return;
		}

		if (!Vel.isZero() || !(flags & MF_NOBLOCKMAP))
		{
			FLinkContext ctx;
			UnlinkFromWorld(&ctx);
			flags |= MF_NOBLOCKMAP;
			SetXYZ(Vec3Offset(Vel));
			CheckPortalTransition(false);
			LinkToWorld(&ctx);
		}
	}
	else
	{
		/*
		{
			// Handle powerup effects here so that the order is controlled
			// by the order in the inventory, not the order in the thinker table
			AActor* item = Inventory;

			while (item != NULL)
			{
				IFVIRTUALPTRNAME(item, NAME_Inventory, DoEffect)
				{
					VMValue params[1] = { item };
					VMCall(func, params, 1, nullptr, 0);
				}
				item = item->Inventory;
			}
		}
		*/

		/*
		if (flags & MF_UNMORPHED)
		{
			return;
		}
		*/

		if (isFrozen())
		{
			return;
		}

		// [RH] Consider carrying sectors here
		DVector2 cumm(0, 0);

		// [RH] If standing on a steep slope, fall down it
		if ((flags & MF_SOLID) && !(flags & (MF_NOCLIP | MF_NOGRAVITY)) &&
			!(flags & MF_NOBLOCKMAP) &&
			Vel.Z <= 0 &&
			floorz == Z())
		{
			secplane_t floorplane;

			// Check 3D floors as well
			floorplane = P_FindFloorPlane(floorsector, PosAtZ(floorz));

			if (floorplane.fC() < STEEPSLOPE &&
				floorplane.ZatPoint(PosRelative(floorsector)) <= floorz)
			{
				const msecnode_t* node;
				bool dopush = true;

				if (floorplane.fC() > STEEPSLOPE * 2 / 3)
				{
					for (node = touching_sectorlist; node; node = node->m_tnext)
					{
						const sector_t* sec = node->m_sector;
						if (sec->floorplane.fC() >= STEEPSLOPE)
						{
							if (floorplane.ZatPoint(PosRelative(node->m_sector)) >= Z() - MaxStepHeight)
							{
								dopush = false;
								break;
							}
						}
					}
				}
				if (dopush)
				{
					Vel += floorplane.Normal().XY();
				}
			}
		}

		// Handle X and Y velocities
		BlockingMobj = nullptr;
		sector_t* oldBlockingCeiling = BlockingCeiling;
		sector_t* oldBlockingFloor = BlockingFloor;
		Blocking3DFloor = nullptr;
		BlockingFloor = nullptr;
		BlockingCeiling = nullptr;
		double oldfloorz = P_XYMovement(this, cumm);
		if (ObjectFlags & OF_EuthanizeMe)
		{ // actor was destroyed
			return;
		}
		// [ZZ] trigger hit floor/hit ceiling actions from XY movement
		if (BlockingFloor && BlockingFloor != oldBlockingFloor && (!player || !(player->cheats & CF_PREDICTING)) && BlockingFloor->SecActTarget)
			BlockingFloor->TriggerSectorActions(this, SECSPAC_HitFloor);
		if (BlockingCeiling && BlockingCeiling != oldBlockingCeiling && (!player || !(player->cheats & CF_PREDICTING)) && BlockingCeiling->SecActTarget)
			BlockingCeiling->TriggerSectorActions(this, SECSPAC_HitCeiling);
		if (Vel.X == 0 && Vel.Y == 0) // Actors at rest
		{
			if (flags2 & MF2_BLASTED)
			{ // Reset to not blasted when velocities are gone
				flags2 &= ~MF2_BLASTED;
			}
			if ((flags6 & MF6_TOUCHY) && !IsSentient())
			{ // Arm a mine which has come to rest
				flags6 |= MF6_ARMED;
			}

		}
		if (Vel.Z != 0 || BlockingMobj || Z() != floorz)
		{	// Handle Z velocity and gravity
			if (((flags2 & MF2_PASSMOBJ) || (flags & MF_SPECIAL)) && !(Level->i_compatflags & COMPATF_NO_PASSMOBJ))
			{
				if (!(onmo = P_CheckOnmobj(this)))
				{
					P_ZMovement(this, oldfloorz);
					flags2 &= ~MF2_ONMOBJ;
				}
				else
				{
					if (onmo->Top() - Z() <= MaxStepHeight)
					{
						SetZ(onmo->Top());
					}
					// Check for MF6_BUMPSPECIAL
					// By default, only players can activate things by bumping into them
					// We trigger specials as long as we are on top of it and not just when
					// we land on it. This could be considered as gravity making us continually
					// bump into it, but it also avoids having to worry about walking on to
					// something without dropping and not triggering anything.
					if ((onmo->flags6 & MF6_BUMPSPECIAL) && ((player != NULL)
						|| ((onmo->activationtype & THINGSPEC_MonsterTrigger) && (flags3 & MF3_ISMONSTER))
						|| ((onmo->activationtype & THINGSPEC_MissileTrigger) && (flags & MF_MISSILE))
						) && (Level->maptime > onmo->lastbump)) // Leave the bumper enough time to go away
					{
						if (player == NULL || !(player->cheats & CF_PREDICTING))
						{
							if (P_ActivateThingSpecial(onmo, this))
								onmo->lastbump = Level->maptime + TICRATE;
						}
					}
					{
						flags2 |= MF2_ONMOBJ;
						Vel.Z = 0;
						Crash();
					}
				}
			}
			else
			{
				P_ZMovement(this, oldfloorz);
			}

			if (ObjectFlags & OF_EuthanizeMe)
				return;		// actor was destroyed
		}
		else if (Z() <= floorz)
		{
			Crash();
			if (ObjectFlags & OF_EuthanizeMe)
				return;		// actor was destroyed
		}

		CheckPortalTransition(true);

		UpdateWaterLevel();

		/*
		// Check for poison damage, but only once per PoisonPeriod tics (or once per second if none).
		if (PoisonDurationReceived && (Level->time % (PoisonPeriodReceived ? PoisonPeriodReceived : TICRATE) == 0))
		{
			P_DamageMobj(this, NULL, Poisoner, PoisonDamageReceived, PoisonDamageTypeReceived != NAME_None ? PoisonDamageTypeReceived : (FName)NAME_Poison, 0);

			--PoisonDurationReceived;

			// Must clear damage when duration is done, otherwise it
			// could be added to with ADDITIVEPOISONDAMAGE.
			if (!PoisonDurationReceived) PoisonDamageReceived = 0;
		}
		*/
	}

	assert(state != NULL);
	if (state == NULL)
	{
		Destroy();
		return;
	}
	if (!CheckNoDelay())
		return; // freed itself
	// cycle through states, calling action functions at transitions

	UpdateRenderSectorList();

	if (tics != -1)
	{
		// [RH] Use tics <= 0 instead of == 0 so that spawnstates
		// of 0 tics work as expected.
		if (--tics <= 0)
		{
			if (!SetState(state->GetNextState()))
				return; 		// freed itself
		}
	}
}
