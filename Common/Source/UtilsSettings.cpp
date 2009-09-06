/*
Copyright_License {

  XCSoar Glide Computer - http://www.xcsoar.org/
  Copyright (C) 2000 - 2009

	M Roberts (original release)
	Robin Birch <robinb@ruffnready.co.uk>
	Samuel Gisiger <samuel.gisiger@triadis.ch>
	Jeff Goodenough <jeff@enborne.f2s.com>
	Alastair Harrison <aharrison@magic.force9.co.uk>
	Scott Penrose <scottp@dd.com.au>
	John Wharington <jwharington@gmail.com>
	Lars H <lars_hn@hotmail.com>
	Rob Dunning <rob@raspberryridgesheepfarm.com>
	Russell King <rmk@arm.linux.org.uk>
	Paolo Ventafridda <coolwind@email.it>
	Tobias Lohner <tobias@lohner-net.de>
	Mirek Jezek <mjezek@ipplc.cz>
	Max Kellermann <max@duempel.org>

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
}
*/

#include "Protection.hpp"
#include "MainWindow.hpp"
#include "SettingsComputer.hpp"
#include "SettingsUser.hpp"
#include "SettingsTask.hpp"
#include "Blackboard.hpp"
#include "RasterTerrain.h"
#include "Waypointparser.h"
#include "AirfieldDetails.h"
#include "Airspace.h"
#include "TopologyStore.h"
#include "McReady.h"
#include "Dialogs.h"
#include "Device/device.h"
#include "Message.h"
#include "Polar/Historical.hpp"
#include "TopologyStore.h"
#include "Components.hpp"
#include "Interface.hpp"

void SettingsEnter() {
  draw_thread->suspend();
  // This prevents the map and calculation threads from doing anything
  // with shared data while it is being changed (also prevents drawing)

  MAPFILECHANGED = FALSE;
  AIRSPACEFILECHANGED = FALSE;
  AIRFIELDFILECHANGED = FALSE;
  WAYPOINTFILECHANGED = FALSE;
  TERRAINFILECHANGED = FALSE;
  TOPOLOGYFILECHANGED = FALSE;
  POLARFILECHANGED = FALSE;
  LANGUAGEFILECHANGED = FALSE;
  STATUSFILECHANGED = FALSE;
  INPUTFILECHANGED = FALSE;
  COMPORTCHANGED = FALSE;
}


void SettingsLeave() {
  if (!globalRunningEvent.test()) return;

  XCSoarInterface::main_window.map.set_focus();

  // mutexing.Lock everything here prevents the calculation thread from running,
  // while shared data is potentially reloaded.

  mutexFlightData.Lock();
  mutexTaskData.Lock();
  mutexNavBox.Lock();

  if(MAPFILECHANGED) {
    AIRSPACEFILECHANGED = TRUE;
    AIRFIELDFILECHANGED = TRUE;
    WAYPOINTFILECHANGED = TRUE;
    TERRAINFILECHANGED = TRUE;
    TOPOLOGYFILECHANGED = TRUE;
  }

  if((WAYPOINTFILECHANGED) || (TERRAINFILECHANGED) || (AIRFIELDFILECHANGED))
    {
      ClearTask();

      // re-load terrain
      terrain.CloseTerrain();
      terrain.OpenTerrain();

      // re-load waypoints
      ReadWayPoints();
      InitWayPointCalc(); // VENTA3
      ReadAirfieldFile();

      // re-set home
      if (WAYPOINTFILECHANGED || TERRAINFILECHANGED) {
	SetHome(XCSoarInterface::SetSettingsComputer(),
		WAYPOINTFILECHANGED==TRUE);
      }

      //
      terrain.ServiceFullReload(XCSoarInterface::Basic().Latitude,
				XCSoarInterface::Basic().Longitude);
    }

  if (TOPOLOGYFILECHANGED)
    {
      topology->Close();
      topology->Open();
    }

  if(AIRSPACEFILECHANGED)
    {
      CloseAirspace();
      ReadAirspace();
      SortAirspace();
    }

  if (POLARFILECHANGED) {
    CalculateNewPolarCoef();
    GlidePolar::UpdatePolar(false, XCSoarInterface::SettingsComputer());
  }

  if (AIRFIELDFILECHANGED
      || AIRSPACEFILECHANGED
      || WAYPOINTFILECHANGED
      || TERRAINFILECHANGED
      || TOPOLOGYFILECHANGED
      ) {
    XCSoarInterface::CloseProgressDialog();
    XCSoarInterface::main_window.map.set_focus();
  }

  mutexNavBox.Unlock();
  mutexTaskData.Unlock();
  mutexFlightData.Unlock();

  if(COMPORTCHANGED) {
    devRestart();
  }

  draw_thread->resume();
  // allow map and calculations threads to continue on their merry way
}


void SystemConfiguration(void) {
#ifndef _SIM_
  if (XCSoarInterface::LockSettingsInFlight 
      && XCSoarInterface::Calculated().Flying) {
    Message::AddMessage(TEXT("Settings locked in flight"));
    return;
  }
#endif
  SettingsEnter();
  dlgConfigurationShowModal();
  SettingsLeave();
}


///////////////////////////////////////////////////////////////////////////////
// settings
int    AutoAdvance = 1;
bool   AdvanceArmed = false;
bool   TaskAborted = false;


// Waypoint Database
int SectorType = 1; // FAI sector
DWORD SectorRadius = 500;
int StartLine = TRUE;
DWORD StartRadius = 3000;

int HomeWaypoint = -1;
int AirfieldsHomeWaypoint = -1; // VENTA3 force Airfields home to be HomeWaypoint if
                                // an H flag in waypoints file is not available..
// Specials
double QFEAltitudeOffset = 0;
#if defined(PNA) || defined(FIVV)
bool needclipping=false; // flag to activate extra clipping for some PNAs
#endif

// user interface settings
bool EnableSoundVario = true;
bool EnableSoundModes = true;
bool EnableSoundTask = true;
bool EnableVarioGauge = false;

// Others
BOOL COMPORTCHANGED = FALSE;
BOOL MAPFILECHANGED = FALSE;
BOOL AIRSPACEFILECHANGED = FALSE;
BOOL AIRFIELDFILECHANGED = FALSE;
BOOL WAYPOINTFILECHANGED = FALSE;
BOOL TERRAINFILECHANGED = FALSE;
BOOL TOPOLOGYFILECHANGED = FALSE;
BOOL POLARFILECHANGED = FALSE;
BOOL LANGUAGEFILECHANGED = FALSE;
BOOL STATUSFILECHANGED = FALSE;
BOOL INPUTFILECHANGED = FALSE;

//Task Information
Task_t Task = {{-1,0,0,0,0,0,0,0,0},{-1,0,0,0,0,0,0,0,0},{-1,0,0,0,0,0,0,0,0},{-1,0,0,0,0,0,0,0,0},{-1,0,0,0,0,0,0,0,0},{-1,0,0,0,0,0,0,0,0},{-1,0,0,0,0,0,0,0,0},{-1,0,0,0,0,0,0,0,0},{-1,0,0,0,0,0,0,0,0},{-1,0,0,0,0,0,0,0,0},{-1,0,0,0,0,0,0,0,0}};
Start_t StartPoints;
TaskStats_t TaskStats;
int ActiveWayPoint = -1;

// Assigned Area Task
double AATTaskLength = 120;
BOOL AATEnabled = FALSE;
DWORD FinishMinHeight = 0;
DWORD StartMaxHeight = 0;
DWORD StartMaxSpeed = 0;
DWORD StartMaxHeightMargin = 0;
DWORD StartMaxSpeedMargin = 0;

