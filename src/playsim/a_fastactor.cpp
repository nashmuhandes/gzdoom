#include <float.h>
#include "templates.h"

#include "m_random.h"
#include "doomdef.h"
#include "p_local.h"

#include "thingdef.h"
#include "d_player.h"
#include "g_levellocals.h"
#include "p_checkposition.h"
#include "p_maputl.h"
#include "p_lnspec.h"

#include "a_fastactor.h"

#define WATER_SINK_FACTOR		0.125
#define WATER_SINK_SMALL_FACTOR	0.25
#define WATER_SINK_SPEED		0.5
#define WATER_JUMP_SPEED		3.5

void P_MonsterFallingDamage(AActor* mo);
double P_XYMovement(AActor* mo);
void P_ZMovement(AActor* mo, double oldfloorz);

//
// P_XYMovement
//
// Returns the actor's old floorz.
//
#define STOPSPEED			(0x1000/65536.)

double P_XYMovement(AActor* mo)
{
	static int pushtime = 0;
	DVector2 ptry;
	player_t* player;
	DVector2 move;
	const secplane_t* walkplane;
	int steps, step, totalsteps;
	DVector2 start;
	double Oldfloorz = mo->floorz;
	double oldz = mo->Z();

	double maxmove = (mo->waterlevel < 1) || (mo->flags & MF_MISSILE) ||
		(mo->player && mo->player->crouchoffset < -10) ? MAXMOVE : MAXMOVE / 4;

	const double VELOCITY_THRESHOLD = 5000;	// don't let it move faster than this. Fixed point overflowed at 32768 but that's too much to make this safe.
	if (mo->Vel.LengthSquared() >= VELOCITY_THRESHOLD * VELOCITY_THRESHOLD)
	{
		mo->Vel.MakeResize(VELOCITY_THRESHOLD);
	}
	move = mo->Vel;

	if (move.isZero())
	{
		return Oldfloorz;
	}

	player = mo->player;

	// [RH] Adjust player movement on sloped floors
	DVector2 startmove = move;
	walkplane = P_CheckSlopeWalk(mo, move);

	// [RH] Take smaller steps when moving faster than the object's size permits.
	// Moving as fast as the object's "diameter" is bad because it could skip
	// some lines because the actor could land such that it is just touching the
	// line. For Doom to detect that the line is there, it needs to actually cut
	// through the actor.

	{
		double maxmove = mo->radius - 1;

		if (maxmove <= 0)
		{ // gibs can have radius 0, so don't divide by zero below!
			maxmove = MAXMOVE;
		}

		const double xspeed = fabs(move.X);
		const double yspeed = fabs(move.Y);

		steps = 1;

		if (xspeed > yspeed)
		{
			if (xspeed > maxmove)
			{
				steps = int(1 + xspeed / maxmove);
			}
		}
		else
		{
			if (yspeed > maxmove)
			{
				steps = int(1 + yspeed / maxmove);
			}
		}
	}

	// P_SlideMove needs to know the step size before P_CheckSlopeWalk
	// because it also calls P_CheckSlopeWalk on its clipped steps.
	DVector2 onestep = startmove / steps;

	start = mo->Pos();
	step = 1;
	totalsteps = steps;

	// [RH] Instead of doing ripping damage each step, do it each tic.
	// This makes it compatible with Heretic and Hexen, which only did
	// one step for their missiles with ripping damage (excluding those
	// that don't use P_XYMovement). It's also more intuitive since it
	// makes the damage done dependant on the amount of time the projectile
	// spends inside a target rather than on the projectile's size. The
	// last actor ripped through is recorded so that if the projectile
	// passes through more than one actor this tic, each one takes damage
	// and not just the first one.
	pushtime++;

	FCheckPosition tm(!!(mo->flags2 & MF2_RIP));

	DAngle oldangle = mo->Angles.Yaw;
	do
	{
		if (mo->Level->i_compatflags & COMPATF_WALLRUN) pushtime++;
		tm.PushTime = pushtime;

		ptry = start + move * step / steps;

		DVector2 startvel = mo->Vel;

		// killough 3/15/98: Allow objects to drop off
		// [RH] If walking on a slope, stay on the slope
		if (!P_TryMove(mo, ptry, true, walkplane, tm))
		{
			// blocked move
			AActor* BlockingMobj = mo->BlockingMobj;
			line_t* BlockingLine = mo->BlockingLine;

			// [ZZ] 
			if (!BlockingLine && !BlockingMobj) // hit floor or ceiling while XY movement - sector actions
			{
				int hitpart = -1;
				sector_t* hitsector = nullptr;
				secplane_t* hitplane = nullptr;
				if (tm.ceilingsector && mo->Z() + mo->Height > tm.ceilingsector->ceilingplane.ZatPoint(tm.pos.XY()))
					mo->BlockingCeiling = tm.ceilingsector;
				if (tm.floorsector && mo->Z() < tm.floorsector->floorplane.ZatPoint(tm.pos.XY()))
					mo->BlockingFloor = tm.floorsector;
				// the following two only set the appropriate field - to avoid issues caused by running actions right in the middle of XY movement
				P_CheckFor3DFloorHit(mo, mo->floorz, false);
				P_CheckFor3DCeilingHit(mo, mo->ceilingz, false);
			}

			if (!(mo->flags & MF_MISSILE) && (mo->BounceFlags & BOUNCE_MBF)
				&& (BlockingMobj != NULL ? P_BounceActor(mo, BlockingMobj, false) : P_BounceWall(mo)))
			{
				// Do nothing, relevant actions already done in the condition.
				// This allows to avoid setting velocities to 0 in the final else of this series.
			}
			else if ((mo->flags2 & (MF2_SLIDE | MF2_BLASTED)) && !(mo->flags & MF_MISSILE))
			{	// try to slide along it
				if (BlockingMobj == NULL)
				{ // slide against wall
					// If the blocked move executed any push specials that changed the
					// actor's velocity, do not attempt to slide.
					if (mo->Vel.XY() == startvel)
					{
						if (player && (mo->Level->i_compatflags & COMPATF_WALLRUN))
						{
							// [RH] Here is the key to wall running: The move is clipped using its full speed.
							// If the move is done a second time (because it was too fast for one move), it
							// is still clipped against the wall at its full speed, so you effectively
							// execute two moves in one tic.
							P_SlideMove(mo, mo->Vel, 1);
						}
						else
						{
							P_SlideMove(mo, onestep, totalsteps);
						}
						if (mo->Vel.XY().isZero())
						{
							steps = 0;
						}
						else
						{
							if (!player || !(mo->Level->i_compatflags & COMPATF_WALLRUN))
							{
								move = mo->Vel;
								onestep = move / steps;
								P_CheckSlopeWalk(mo, move);
							}
							start = mo->Pos().XY() - move * step / steps;
						}
					}
					else
					{
						steps = 0;
					}
				}
				else
				{ // slide against another actor
					DVector2 t;
					t.X = 0, t.Y = onestep.Y;
					walkplane = P_CheckSlopeWalk(mo, t);
					if (P_TryMove(mo, mo->Pos() + t, true, walkplane, tm))
					{
						mo->Vel.X = 0;
					}
					else
					{
						t.X = onestep.X, t.Y = 0;
						walkplane = P_CheckSlopeWalk(mo, t);
						if (P_TryMove(mo, mo->Pos() + t, true, walkplane, tm))
						{
							mo->Vel.Y = 0;
						}
						else
						{
							mo->Vel.X = mo->Vel.Y = 0;
						}
					}
					steps = 0;
				}
			}
			else
			{
				mo->Vel.X = mo->Vel.Y = 0;
				steps = 0;
			}
		}
		else
		{
			if (mo->Pos().XY() != ptry)
			{
				// If the new position does not match the desired position, the player
				// must have gone through a teleporter or portal.

				if (mo->Vel.X == 0 && mo->Vel.Y == 0)
				{
					// Stop moving right now if it was a regular teleporter.
					step = steps;
				}
				else
				{
					// It was a portal, line-to-line or fogless teleporter, so the move should continue.
					// For that we need to adjust the start point, and the movement vector.
					DAngle anglediff = deltaangle(oldangle, mo->Angles.Yaw);

					if (anglediff != 0)
					{
						move = move.Rotated(anglediff);
						oldangle = mo->Angles.Yaw;
					}
					start = mo->Pos() - move * step / steps;
				}
			}
		}
	} while (++step <= steps);

	// Friction

	if (mo->Z() > mo->floorz && !(mo->flags2 & MF2_ONMOBJ) &&
		!mo->IsNoClip2() &&
		(!(mo->flags2 & MF2_FLY) || !(mo->flags & MF_NOGRAVITY)) &&
		!mo->waterlevel)
	{ // [RH] Friction when falling is available for larger aircontrols
		return Oldfloorz;
	}

	// killough 8/11/98: add bouncers
	// killough 9/15/98: add objects falling off ledges
	// killough 11/98: only include bouncers hanging off ledges
	if ((mo->flags & MF_CORPSE) || (mo->flags6 & MF6_FALLING))
	{ // Don't stop sliding if halfway off a step with some velocity
		if (fabs(mo->Vel.X) > 0.25 || fabs(mo->Vel.Y) > 0.25)
		{
			if (mo->floorz > mo->Sector->floorplane.ZatPoint(mo))
			{
				if (mo->dropoffz != mo->floorz) // 3DMidtex or other special cases that must be excluded
				{
					unsigned i;
					for (i = 0; i < mo->Sector->e->XFloor.ffloors.Size(); i++)
					{
						// Sliding around on 3D floors looks extremely bad so
						// if the floor comes from one in the current sector stop sliding the corpse!
						F3DFloor* rover = mo->Sector->e->XFloor.ffloors[i];
						if (!(rover->flags & FF_EXISTS)) continue;
						if (rover->flags & FF_SOLID && rover->top.plane->ZatPoint(mo) == mo->floorz) break;
					}
					if (i == mo->Sector->e->XFloor.ffloors.Size())
						return Oldfloorz;
				}
			}
		}
	}

	{
		// phares 3/17/98
		// Friction will have been adjusted by friction thinkers for icy
		// or muddy floors. Otherwise it was never touched and
		// remained set at ORIG_FRICTION
		//
		// killough 8/28/98: removed inefficient thinker algorithm,
		// instead using touching_sectorlist in P_GetFriction() to
		// determine friction (and thus only when it is needed).
		//
		// killough 10/98: changed to work with new bobbing method.
		// Reducing player velocity is no longer needed to reduce
		// bobbing, so ice works much better now.

		double friction = P_GetFriction(mo, NULL);

		mo->Vel.X *= friction;
		mo->Vel.Y *= friction;
	}
	return Oldfloorz;
}

//
// P_ZMovement
//

void P_ZMovement(AActor* mo, double oldfloorz)
{
	double dist;
	double delta;
	double oldz = mo->Z();
	double grav = mo->GetGravity();


	mo->AddZ(mo->Vel.Z);

	//
	// apply gravity
	//
	if (mo->Z() > mo->floorz && !(mo->flags & MF_NOGRAVITY))
	{
		double startvelz = mo->Vel.Z;

		if (mo->waterlevel == 0 || (mo->player &&
			!(mo->player->cmd.ucmd.forwardmove | mo->player->cmd.ucmd.sidemove)))
		{
			// [RH] Double gravity only if running off a ledge. Coming down from
			// an upward thrust (e.g. a jump) should not double it.
			if (mo->Vel.Z == 0 && oldfloorz > mo->floorz && mo->Z() == oldfloorz)
			{
				mo->Vel.Z -= grav + grav;
			}
			else
			{
				mo->Vel.Z -= grav;
			}
		}
		if (mo->player == NULL)
		{
			if (mo->waterlevel >= 1)
			{
				double sinkspeed;

				if ((mo->flags & MF_SPECIAL) && !(mo->flags3 & MF3_ISMONSTER))
				{ // Pickup items don't sink if placed and drop slowly if dropped
					sinkspeed = (mo->flags & MF_DROPPED) ? -WATER_SINK_SPEED / 8 : 0;
				}
				else
				{
					sinkspeed = -WATER_SINK_SPEED;

					// If it's not a player, scale sinkspeed by its mass, with
					// 100 being equivalent to a player.
					if (mo->player == NULL)
					{
						sinkspeed = sinkspeed * clamp(mo->Mass, 1, 4000) / 100;
					}
				}
				if (mo->Vel.Z < sinkspeed)
				{ // Dropping too fast, so slow down toward sinkspeed.
					mo->Vel.Z -= MAX(sinkspeed * 2, -8.);
					if (mo->Vel.Z > sinkspeed)
					{
						mo->Vel.Z = sinkspeed;
					}
				}
				else if (mo->Vel.Z > sinkspeed)
				{ // Dropping too slow/going up, so trend toward sinkspeed.
					mo->Vel.Z = startvelz + MAX(sinkspeed / 3, -8.);
					if (mo->Vel.Z < sinkspeed)
					{
						mo->Vel.Z = sinkspeed;
					}
				}
			}
		}
	}

	if (mo->waterlevel && !(mo->flags & MF_NOGRAVITY) && !(mo->flags8 & MF8_NOFRICTION))
	{
		double friction = -1;

		// Check 3D floors -- might be the source of the waterlevel
		for (auto rover : mo->Sector->e->XFloor.ffloors)
		{
			if (!(rover->flags & FF_EXISTS)) continue;
			if (!(rover->flags & FF_SWIMMABLE)) continue;

			if (mo->Z() >= rover->top.plane->ZatPoint(mo) ||
				mo->Center() < rover->bottom.plane->ZatPoint(mo))
				continue;

			friction = rover->model->GetFriction(rover->top.isceiling);
			break;
		}
		if (friction < 0)
			friction = mo->Sector->GetFriction();	// get real friction, even if from a terrain definition

		mo->Vel.Z *= friction;
	}

	//
	// clip movement
	//
	if (mo->Z() <= mo->floorz)
	{	// Hit the floor
		if ((!mo->player || !(mo->player->cheats & CF_PREDICTING)) &&
			mo->Sector->SecActTarget != NULL &&
			mo->Sector->floorplane.ZatPoint(mo) == mo->floorz)
		{ // [RH] Let the sector do something to the actor
			mo->Sector->TriggerSectorActions(mo, SECSPAC_HitFloor);
		}
		P_CheckFor3DFloorHit(mo, mo->floorz, true);
		// [RH] Need to recheck this because the sector action might have
		// teleported the actor so it is no longer below the floor.
		if (mo->Z() <= mo->floorz)
		{
			mo->BlockingFloor = mo->Sector;
			if (mo->flags3 & MF3_ISMONSTER)		// Blasted mobj falling
			{
				if (mo->Vel.Z < -23)
				{
					P_MonsterFallingDamage(mo);
				}
			}
			mo->SetZ(mo->floorz);
			if (mo->Vel.Z < 0)
			{
				const double minvel = -8;	// landing speed from a jump with normal gravity

				// Spawn splashes, etc.
				P_HitFloor(mo);
				if (mo->DamageType == NAME_Ice && mo->Vel.Z < minvel)
				{
					mo->tics = 1;
					mo->Vel.Zero();
					return;
				}
				// Let the actor do something special for hitting the floor
				if (mo->flags7 & MF7_SMASHABLE)
				{
					P_DamageMobj(mo, nullptr, nullptr, mo->health, NAME_Smash);
				}
				mo->Vel.Z = 0;
			}
			mo->Crash();
		}
	}

	if (mo->flags2 & MF2_FLOORCLIP)
	{
		mo->AdjustFloorClip();
	}

	if (mo->Top() > mo->ceilingz)
	{ // hit the ceiling
		if ((!mo->player || !(mo->player->cheats & CF_PREDICTING)) &&
			mo->Sector->SecActTarget != NULL &&
			mo->Sector->ceilingplane.ZatPoint(mo) == mo->ceilingz)
		{ // [RH] Let the sector do something to the actor
			mo->Sector->TriggerSectorActions(mo, SECSPAC_HitCeiling);
		}
		P_CheckFor3DCeilingHit(mo, mo->ceilingz, true);
		// [RH] Need to recheck this because the sector action might have
		// teleported the actor so it is no longer above the ceiling.
		if (mo->Top() > mo->ceilingz)
		{
			mo->BlockingCeiling = mo->Sector;
			mo->SetZ(mo->ceilingz - mo->Height);
			if (mo->Vel.Z > 0)
				mo->Vel.Z = 0;
		}
	}
	P_CheckFakeFloorTriggers(mo, oldz);
}

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
		if (!player || !(player->cheats & CF_PREDICTING))
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

		if (isFrozen())
		{
			return;
		}

		double oldz = Z();

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
		double oldfloorz = P_XYMovement(this);
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
						if (player && player->mo == this)
						{
							player->viewheight -= onmo->Top() - Z();
							double deltaview = player->GetDeltaViewHeight();
							if (deltaview > player->deltaviewheight)
							{
								player->deltaviewheight = deltaview;
							}
						}
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
