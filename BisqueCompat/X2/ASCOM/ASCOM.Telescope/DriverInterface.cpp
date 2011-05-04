//========================================================================
//
// TITLE:		DriverInterface.cpp
//
// FACILITY:	X2 Plugin for TheSky X and ASCOM drivers
//
// ABSTRACT:	Provides COM calls to the Telescope driver, and various COM
//				related functions. This is "ATL-free" for lightness of
//				weight, so you see "bare-metal COM" here. This was a 
//				conscious design choice.
//
// USING:		Global Interface Table to marshal driver interface across 
//				threads. See http://msdn.microsoft.com/en-us/library/ms678517
//
// ENVIRONMENT:	Microsoft Windows XP/Vista/7
//				Developed under Microsoft Visual C++ 9 (VS2008)
//
// AUTHOR:		Robert B. Denny
//
// Edit Log:
//
// When			Who		What
//----------	---		--------------------------------------------------
// 22-Apr-11	rbd		Initial edit, taken from TeleAPI/ASCOM plugin
// 27-Apr-11	rbd		Much work, refactoring. Work in progress
//========================================================================

#include "StdAfx.h"

#define CROSS_THREAD

#define OUR_REGISTRY_BASE HKEY_LOCAL_MACHINE
#define OUR_REGISTRY_AREA "Software\\ASCOM\\TheSky X2\\Mount"
#define OUR_DRIVER_SEL "Current Driver ID"

const char *_szAlertTitle = "ASCOM Standard Telescope";

//
// Mount characteristics
//
char *_szScopeName = NULL;										// Name for crosshair labeling (delete[])
char *_szScopeDescription = NULL;
char *_szScopeDriverInfo = NULL;
char *_szScopeDriverVersion = NULL;
char _szDriverID[256];
bool _bScopeCanSlew = false;									// Capabilites of this scope
bool _bScopeCanSlewAsync = false;
bool _bScopeCanSlewAltAz = false;				
bool _bScopeCanSync = false;
bool _bScopeIsGEM = false;
bool _bScopeCanSetTracking = false;
bool _bScopeCanSetTrackRates = false;
bool _bScopeCanPark = false;
bool _bScopeCanUnpark = false;
bool _bScopeCanSetPark = false;
bool _bScopeDoesRefraction = false;
bool _bScopeCanSideOfPier = false;

//
// State variables
//
bool _bScopeActive = false;										// This is true if mount is active

static int get_integer(OLECHAR *name);
static double get_double(OLECHAR *name);
static void set_double(OLECHAR *name, double val);
static bool get_bool(OLECHAR *name);
static void set_bool(OLECHAR *name, bool val);
static char *get_string(OLECHAR *name);
static void switchThreadIf();
static void get_driverid(char *id, bool forConfig);
static void save_driverid(char *id);

static IDispatch *_p_DrvDisp = NULL;							// [sentinel] Pointer to driver interface

#ifdef CROSS_THREAD
static IGlobalInterfaceTable *_p_GIT;							// Pointer to Global Interface Table interface
static DWORD dwIntfcCookie;										// Driver interface cookie for GIT
static DWORD dCurrIntfcThreadId;								// ID of thread on which the interface is currently marshalled
#endif
static bool bSyncSlewing = false;								// True if scope is doing a sync slew
static LoggerInterface *pLog;

// -------------
// InitDrivers()
// -------------
//
// Really just starts OLE
//
bool InitDrivers(LoggerInterface *pLogger)
{
	DWORD dwVer;
	bool bResult = true;
	static bool bInitDone = false;								// Exec this only once

	pLog = pLogger;
	if(!bInitDone)
	{
		__try
		{
			SetMessageQueue(96);
			dwVer = CoBuildVersion();
			if(rmm != HIWORD(dwVer))
				drvFail("Wrong version of OLE", NULL, true);
			if(FAILED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED)))
				drvFail("Failed to start OLE", NULL, true);
#ifdef CROSS_THREAD
			if(FAILED(CoCreateInstance(CLSID_StdGlobalInterfaceTable,
						NULL,
						CLSCTX_INPROC_SERVER,
						IID_IGlobalInterfaceTable,
						(void **)&_p_GIT)))
				drvFail("Failed to connect to Global Interface Table", NULL, true);
#endif
			bInitDone = true;
			get_driverid(_szDriverID, true);						// Get any saved ProgID or ""
//pLog->out("Get RA");
//pLog->packetsRetriesFailuresChanged(0, 0, 0);
		}
		__except(EXCEPTION_EXECUTE_HANDLER)
		{
			bResult = false;	// Failed to initialize
		}
	}

	return bResult;
}

// ---------
// InitScope
// ---------
//
// Here is where we fire up COM and create an instance of the scope driver.
//
short InitScope(void)
{
	CLSID CLSID_driver;
	short iRes = 0;				// Assume success (our retval)
	OLECHAR *ocProgID = NULL;

	__try {
		_bScopeActive = false;									// Assume failure

		//
		// Retrieve the ProgID of the driver we're to use. Get it into 
		// OLESTR format.
		//
		get_driverid(_szDriverID, false);						// false -> must have an ID saved
		ocProgID = ansi_to_uni(_szDriverID);

		//
		// Create an instance of our ASCOM driver.
		//
		if(FAILED(CLSIDFromProgID(ocProgID, &CLSID_driver)))
		{
			char buf[256];

			wsprintf(buf, "Failed to find scope driver %s.", _szDriverID);
			free(ocProgID);
			drvFail(buf, NULL, true);
		}
		free(ocProgID);

		if(FAILED(CoCreateInstance(
			CLSID_driver,
			NULL,
			CLSCTX_SERVER,
			IID_IDispatch,
			(LPVOID *)&_p_DrvDisp)))
		{
			char buf[256];

			wsprintf(buf, "Failed to create an instance of the scope driver %s.", _szDriverID);
			drvFail(buf, NULL, true);
		}

#ifdef CROSS_THREAD
		//
		// Get the marshalled interface pointer for later possible thread switching
		//
		dCurrIntfcThreadId = GetCurrentThreadId();
		if(FAILED(_p_GIT->RegisterInterfaceInGlobal(
				_p_DrvDisp,
				IID_IDispatch,
				&dwIntfcCookie)))
			drvFail("Failed to register driver interface in GIT", NULL, true);
#endif
		//
		// We now need to connect the scope. To do this, we set the 
		// Connected property to TRUE.
		//
		set_bool(L"Connected", true);

		//
		// At this point we should be able to call into the ASCOM scope
		// driver through its IDIspatch. Check to see if things are OK. 
		// We call GetName, because the driver MUST support that, then
		// grab the capabilities we need (these also MUST be supported).
		//
		_szScopeName = get_string(L"Name");						// Indicator label/name
		_szScopeDescription = get_string(L"Description");
		_szScopeDriverInfo = get_string(L"DriverInfo");
		_szScopeDriverVersion = get_string(L"DriverVersion");
		_bScopeCanSync = get_bool(L"CanSync");					// Can it sync?
		_bScopeCanSlew = get_bool(L"CanSlew");					// Can it slew at all?	
		_bScopeCanSlewAsync = get_bool(L"CanSlewAsync");		// Can it slew asynchronously?
		_bScopeCanSlewAltAz = get_bool(L"CanSlewAltAz");
		_bScopeIsGEM = (get_integer(L"AlignmentMode") == 2);	// Is it a GEM?
		_bScopeCanSetTracking = get_bool(L"CanSetTracking");	// Can we control its tracking?
		_bScopeCanSetTrackRates = get_bool(L"CanSetRightAscensionRate") &&
							get_bool(L"CanSetDeclinationRate");	// Too bad for mounts that can't do both
		_bScopeCanPark = get_bool(L"CanPark");
		_bScopeCanUnpark = get_bool(L"CanUnpark");
		_bScopeCanSetPark = get_bool(L"CanSetPark");
		_bScopeDoesRefraction = get_bool(L"DoesRefraction");

		//
		// Now we verify that there is a scope out there. We try to get
		// RA, and if there's a problem, we've had it.
		//
		__try {
			GetRightAscension();
		} __except(EXCEPTION_EXECUTE_HANDLER) {
			ABORT;												// Some real error, has been alerted
		}
		//
		// Determine if it reports SOP (pointing state)
		//
		__try {
			get_integer(L"SideOfPier");
			_bScopeCanSideOfPier = true;
		} __except(EXCEPTION_EXECUTE_HANDLER) {
			_bScopeCanSideOfPier = false;
		}
		//
		// If the scope can be unparked, do it. 
		//
		//if(_bScopeCanUnpark)
		//	UnparkScope();
		//
		// In order for TheSky X to complete startup and change status
		// from Connecting... tracking must be on or it must be parked.
		//
		if(_bScopeCanSetTracking && 
					(!_bScopeCanPark || !get_bool(L"AtPark")) && 
					!get_bool(L"Tracking"))
			set_bool(L"Tracking", true);
		//
		// If the scope has tracking rates, turn them off
		//
		if(_bScopeCanSetTrackRates)
		{
			set_double(L"RightAscensionRate", 0.0);
			set_double(L"DeclinationRate", 0.0);
		}
		//
		// Done!
		//
		_bScopeActive = true;									// We're off and running!
	}
	__except(EXCEPTION_EXECUTE_HANDLER)
	{
		_bScopeActive = false;
		iRes = -1;			// What are the errors?
	}

	return(iRes);
}

// ---------
// TermScope
// ---------
//
void TermScope(bool fatal)
{
	OLECHAR *name = L"Connected";
	DISPID dispid;
	DISPID didPut = DISPID_PROPERTYPUT;
	DISPPARAMS dispParms;
	VARIANTARG rgvarg[1];
	EXCEPINFO excep;
	VARIANT vRes;
	HRESULT hr;

	if(_p_DrvDisp != NULL)										// Just in case! (see termPlugin())
	{
#ifdef CROSS_THREAD
		switchThreadIf();
#endif

		//
		// We now need to unconnect the scope. To do this, we set the 
		// Connected property to FALSE. THe use of !fatal in the calls
		// to drvFail assues that drvFail() will not recursively call 
		// US because its call to TermScope() passses true, while all
		// others pass false.
		//
		if(FAILED(_p_DrvDisp->GetIDsOfNames(
			IID_NULL, 
			&name, 
			1, 
			LOCALE_USER_DEFAULT,
			&dispid)))
			drvFail(
				"The ASCOM scope driver is missing the Connected property.",
				NULL, !fatal);

		rgvarg[0].vt = VT_BOOL;
		rgvarg[0].boolVal = VARIANT_FALSE;
		dispParms.cArgs = 1;
		dispParms.rgvarg = rgvarg;
		dispParms.cNamedArgs = 1;								// PropPut kludge
		dispParms.rgdispidNamedArgs = &didPut;
		if(FAILED(hr = _p_DrvDisp->Invoke(
			dispid,
			IID_NULL, 
			LOCALE_USER_DEFAULT, 
			DISPATCH_PROPERTYPUT, 
			&dispParms, 
			&vRes,
			&excep, NULL)))
			drvFail("the Connected = False failed internally.", &excep, true);	// Don't call us back!


#ifdef CROSS_THREAD
		_p_GIT->RevokeInterfaceFromGlobal(dwIntfcCookie);		// We're done with this driver/object
#endif

		_p_DrvDisp->Release();									// Release instance of the driver
		_p_DrvDisp = NULL;										// So won't happen again in PROCESS_DETACH
	}
	if(_szScopeName != NULL)
		delete[] _szScopeName;									// Free this string
	_szScopeName = NULL;										// [sentinel]
	if(_szScopeDescription != NULL)
		delete[] _szScopeDescription;
	_szScopeDescription = NULL;
	if(_szScopeDriverInfo != NULL)
		delete[] _szScopeDriverInfo;
	_szScopeDriverInfo = NULL;
	if(_szScopeDriverVersion != NULL)
		delete[] _szScopeDriverVersion;
	_szScopeDriverVersion = NULL;


	_bScopeActive = false;

}

// --------------
// GetCanPierSide
// --------------
//
bool GetCanPierSide(void)
{
	return (_bScopeIsGEM && _bScopeCanSideOfPier);
}

// -----------------
// GetRightAscension
// -----------------
//
double GetRightAscension(void)
{
	return(get_double(L"RightAscension"));
}

// ---------------------
// GetRightAscensionRate
// ---------------------
//
double GetRightAscensionRate(void)
{
	return(get_double(L"RightAscensionRate"));
}

// --------------
// GetDeclination
// --------------
//
double GetDeclination(void)
{
	return(get_double(L"Declination"));
}

// ------------------
// GetDeclinationRate
// ------------------
//
double GetDeclinationRate(void)
{
	return(get_double(L"DeclinationRate"));
}

// ---------
// GetAtPark
// ---------
//
bool GetAtPark(void)
{
	return(get_bool(L"AtPark"));
}

// -----------
// GetTracking
// -----------
bool GetTracking(void)
{
	return(get_bool(L"Tracking"));
}

// -----------
// SetTracking
// -----------
void SetTracking(bool state)
{
	set_bool(L"Tracking", state);
}

// ---------------------
// SetRightAscensionRate
// ---------------------
//
void SetRightAscensionRate(double rate)
{
	set_double(L"RightAscensionRate", rate);
}

// ------------------
// SetDeclinationRate
// ------------------
//
void SetDeclinationRate(double rate)
{
	set_double(L"DeclinationRate", rate);
}

// -----------
// SetLatitude
// -----------
//
void SetLatitude(double lat)
{
	set_double(L"SiteLatitude", lat);
}

// ------------
// SetLongitude
// ------------
//
void SetLongitude(double lng)
{
	set_double(L"SiteLongitude", lng);
}

// ----------
// IsPierWest
// ----------
//
bool IsPierWest(void)
{
	int sp = get_integer(L"SideOfPier");
	return(sp == 1);
}

// ---------
// IsSlewing
// ---------
//
bool IsSlewing(void)
{
	return get_bool(L"Slewing");
}

//
//	----
//	Slew
//	----
//
//	Slew the telescope to a specified RA/Dec position.
//
short SlewScope(double dRA, double dDec)
{
	OLECHAR *name;
	DISPID dispid;
	DISPPARAMS dispParms;
	VARIANTARG rgvarg[2];
	EXCEPINFO excep;
	VARIANT vRes;
	short iRes = 0;												// Assume success (our retval)
	HRESULT hr;

	if(!_bScopeActive)											// No scope hookup?
	{
		bSyncSlewing = false;									// Cannot be sync-slewing!
		return(-1);												// Forget this
	}

	//
	// Fall back to sync slewing if async not supported.
	//
	if(_bScopeCanSlewAsync)
		name = L"SlewToCoordinatesAsync";
	else
		name = L"SlewToCoordinates";

	__try
	{

#ifdef CROSS_THREAD
		switchThreadIf();
#endif

		//
		// Get our dispatch ID
		//
		if(FAILED(_p_DrvDisp->GetIDsOfNames(
			IID_NULL, 
			&name, 
			1, 
			LOCALE_USER_DEFAULT,
			&dispid)))
		{
			char buf[256];

			wsprintf(buf, "The ASCOM scope driver is missing the %s method.", name);
			drvFail(buf, NULL, true);
		}
		//
		// Start the slew.
		//
		rgvarg[0].vt = VT_R8;									// Arg order is R->L
		rgvarg[0].dblVal = dDec;
		rgvarg[1].vt = VT_R8;
		rgvarg[1].dblVal = dRA;
		dispParms.cArgs = 2;
		dispParms.rgvarg = rgvarg;
		dispParms.cNamedArgs = 0;
		dispParms.rgdispidNamedArgs =NULL;
		if(!_bScopeCanSlewAsync) bSyncSlewing = true;			// Internal flag
		if(FAILED(hr = _p_DrvDisp->Invoke(
			dispid,
			IID_NULL, 
			LOCALE_USER_DEFAULT, 
			DISPATCH_METHOD, 
			&dispParms, 
			&vRes,
			&excep, 
			NULL)))
		{
			//
			// Most slew failures are not fatal (below horizon, etc.)
			// Report the error and let the driver continue.
			//
			drvFail("Slew to object failed internally.", &excep, false);
		}
	}
	__except(EXCEPTION_EXECUTE_HANDLER)
	{
		iRes = -1;
	}

	bSyncSlewing = false;

	return(iRes);
}

// ---------
// AbortSlew
// ---------
//
void AbortSlew(void)
{
	OLECHAR *name = L"AbortSlew";
	DISPID dispid;
	DISPPARAMS dispparms;
	EXCEPINFO excep;
	VARIANT vRes;

#ifdef CROSS_THREAD
	switchThreadIf();
#endif

	//
	// Get our dispatch ID
	//
	if(FAILED(_p_DrvDisp->GetIDsOfNames(
		IID_NULL, 
		&name,
		1, 
		LOCALE_USER_DEFAULT,
		&dispid)))
		drvFail(
			"The ASCOM scope driver is missing the AbortSlew method.",
			NULL, true);

	//
	// No dispatch parameters for propget
	//
	dispparms.cArgs = 0;
	dispparms.rgvarg = NULL;
	dispparms.cNamedArgs = 0;
	dispparms.rgdispidNamedArgs = NULL;
	//
	// Invoke the method
	//
	if(FAILED(_p_DrvDisp->Invoke(dispid, 
				     IID_NULL, 
				     LOCALE_USER_DEFAULT, 
				     DISPATCH_METHOD,
				     &dispparms, 
				     &vRes, 
				     &excep, 
				     NULL)))
		drvFail("AbortSlew failed internally.", &excep, true);
		
}

//	---------
//	SyncScope
//	---------
//
//	Sync the telescope's position to the given coordinates
//
//
short SyncScope(double dRA, double dDec)
{
	OLECHAR *name = L"SyncToCoordinates";
	DISPID dispid;
	DISPPARAMS dispParms;
	VARIANTARG rgvarg[2];
	EXCEPINFO excep;
	VARIANT vRes;
	short iRes = 0;												// Assume success (our retval)
	HRESULT hr;

	if(!_bScopeActive)											// No scope hookup?
		return(-1);												// Forget this

	__try
		{
#ifdef CROSS_THREAD
			switchThreadIf();
#endif

			//
			// Get our dispatch ID
			//
			if(FAILED(_p_DrvDisp->GetIDsOfNames(
				IID_NULL, 
				&name, 
				1, 
				LOCALE_USER_DEFAULT,
				&dispid)))
				drvFail(
					"The ASCOM scope driver is missing the SyncToCoordinates method.",
					NULL, true);

			//
			// Do the sync
			//
			rgvarg[0].vt = VT_R8;		// Arg order is R->L
			rgvarg[0].dblVal = dDec;
			rgvarg[1].vt = VT_R8;
			rgvarg[1].dblVal = dRA;
			dispParms.cArgs = 2;
			dispParms.rgvarg = rgvarg;
			dispParms.cNamedArgs = 0;
			dispParms.rgdispidNamedArgs =NULL;
			if(FAILED(hr = _p_DrvDisp->Invoke(
				dispid, 
				IID_NULL, 
				LOCALE_USER_DEFAULT, 
				DISPATCH_METHOD, 
				&dispParms, 
				&vRes,
				&excep, 
				NULL)))
			{
				//
				// All errors fatal. Should not call this if _bScopeCanSync is false!
				//
				drvFail("Sync to coordinates failed internally.", &excep, true);
			}
		}
	__except(EXCEPTION_EXECUTE_HANDLER)
		{
			iRes = -1;
		}

	return(iRes);
}	

// ---------
// ParkScope
// ---------
//
void ParkScope(void)
{
	OLECHAR *name = L"Park";
	DISPID dispid;
	DISPPARAMS dispparms;
	EXCEPINFO excep;
	VARIANT vRes;

#ifdef CROSS_THREAD
	switchThreadIf();
#endif

	//
	// Get our dispatch ID
	//
	if(FAILED(_p_DrvDisp->GetIDsOfNames(
		IID_NULL, 
		&name,
		1, 
		LOCALE_USER_DEFAULT,
		&dispid)))
		drvFail(
			"The ASCOM scope driver is missing the Park method.",
			NULL, true);

	//
	// No dispatch parameters for propget
	//
	dispparms.cArgs = 0;
	dispparms.rgvarg = NULL;
	dispparms.cNamedArgs = 0;
	dispparms.rgdispidNamedArgs = NULL;
	//
	// Invoke the method
	//
	if(FAILED(_p_DrvDisp->Invoke(dispid, 
				     IID_NULL, 
				     LOCALE_USER_DEFAULT, 
				     DISPATCH_METHOD,
				     &dispparms, 
				     &vRes, 
				     &excep, 
				     NULL)))
		drvFail("Park failed internally.", &excep, true);
		
}

// -----------
// UnparkScope
// -----------
//
void UnparkScope(void)
{
	OLECHAR *name = L"Unpark";
	DISPID dispid;
	DISPPARAMS dispparms;
	EXCEPINFO excep;
	VARIANT vRes;

#ifdef CROSS_THREAD
	switchThreadIf();
#endif

	//
	// Get our dispatch ID
	//
	if(FAILED(_p_DrvDisp->GetIDsOfNames(
		IID_NULL, 
		&name,
		1, 
		LOCALE_USER_DEFAULT,
		&dispid)))
		drvFail(
			"The ASCOM scope driver is missing the Unpark method.",
			NULL, true);

	//
	// No dispatch parameters for propget
	//
	dispparms.cArgs = 0;
	dispparms.rgvarg = NULL;
	dispparms.cNamedArgs = 0;
	dispparms.rgdispidNamedArgs = NULL;
	//
	// Invoke the method
	//
	if(FAILED(_p_DrvDisp->Invoke(dispid, 
				     IID_NULL, 
				     LOCALE_USER_DEFAULT, 
				     DISPATCH_METHOD,
				     &dispparms, 
				     &vRes, 
				     &excep, 
				     NULL)))
		drvFail("Unpark failed internally.", &excep, true);
		
}

// ------------
// SetParkScope
// ------------
//
void SetParkScope(void)
{
	OLECHAR *name = L"SetPark";
	DISPID dispid;
	DISPPARAMS dispparms;
	EXCEPINFO excep;
	VARIANT vRes;

#ifdef CROSS_THREAD
	switchThreadIf();
#endif

	//
	// Get our dispatch ID
	//
	if(FAILED(_p_DrvDisp->GetIDsOfNames(
		IID_NULL, 
		&name,
		1, 
		LOCALE_USER_DEFAULT,
		&dispid)))
		drvFail(
			"The ASCOM scope driver is missing the SetPark method.",
			NULL, true);

	//
	// No dispatch parameters for propget
	//
	dispparms.cArgs = 0;
	dispparms.rgvarg = NULL;
	dispparms.cNamedArgs = 0;
	dispparms.rgdispidNamedArgs = NULL;
	//
	// Invoke the method
	//
	if(FAILED(_p_DrvDisp->Invoke(dispid, 
				     IID_NULL, 
				     LOCALE_USER_DEFAULT, 
				     DISPATCH_METHOD,
				     &dispparms, 
				     &vRes, 
				     &excep, 
				     NULL)))
		drvFail("SetPark failed internally.", &excep, true);
		
}

// -----------
// ConfigScope
// -----------
//
// Use the ASCOM Scope Chooser to get the ProgID of the driver to use.
// The ProgID is stored in the registry and used by InitScope().
// The Chooser also provides a config button for the scope itself.
//
short ConfigScope()
{
	CLSID CLSID_chooser;
	OLECHAR *name = L"Choose";
	DISPID dispid;
	DISPPARAMS dispParms;
	EXCEPINFO excep;
	VARIANTARG rgvarg[1];										// Chooser.Choose(ProgID)
	VARIANT vRes;
	HRESULT hr;
	short iRes = 0;												// Assume success (our retval)
	IDispatch *pChsrDsp = NULL;									// [sentinel]
	char *cp = NULL;											// [sentinel]
	BSTR bsProgID = NULL;										// [sentinel]
	
	__try {
		
		_szDriverID[0] = '\0';									// Assume no current ProgID
		bsProgID = SysAllocString(L"");
		get_driverid(_szDriverID, true);						// Get any saved ProgID or ""
		if (_szDriverID[0] != '\0')								// If we have a saved ID
		{
			SysFreeString(bsProgID);
			bsProgID = ansi_to_bstr(_szDriverID);				// Use it for the Chooser
		}

		//
		// Create an instance of the ASCOM Scope Chooser.
		//
		if(FAILED(CLSIDFromProgID(L"DriverHelper.Chooser", &CLSID_chooser)))
			drvFail(
				"Failed to find the ASCOM Scope Chooser component. Is it installed?", 
				NULL, true);

		if(FAILED(CoCreateInstance(
			CLSID_chooser,
			NULL,
			CLSCTX_SERVER,
			IID_IDispatch,
			(LPVOID *)&pChsrDsp)))
			drvFail(
				"Failed to create an instance of the ASCOM Scope Chooser. Is it installed?", 
				NULL, true);

		//
		// Now just call the Choose() method. It returns a BSTR 
		// containing the ProgId to use for the selected scope 
		// driver.
		//
		if(FAILED(pChsrDsp->GetIDsOfNames(
			IID_NULL, 
			&name, 
			1, 
			LOCALE_USER_DEFAULT,
			&dispid)))
			drvFail(
				"The ASCOM Scope Chooser is missing the Choose method.",
				NULL, true);

		//
		// Call the Choose() method with our ProgID string
		//
		rgvarg[0].vt = VT_BSTR;
		rgvarg[0].bstrVal = bsProgID;
		dispParms.cArgs = 1;
		dispParms.rgvarg = rgvarg;
		dispParms.cNamedArgs = 0;
		dispParms.rgdispidNamedArgs = NULL;
		if(FAILED(hr = pChsrDsp->Invoke(
			dispid,
			IID_NULL, 
			LOCALE_USER_DEFAULT, 
			DISPATCH_METHOD, 
			&dispParms, 
			&vRes,
			&excep, NULL)))
			drvFail("The Choose() method failed internally.", &excep, true);

		//
		// At this point, the variant vRes contains a BSTR with the 
		// ProgID of the selected driver, or an empty BSTR. Fail if 
		// the method returned something other than a bstr!
		//
		if(vRes.vt != VT_BSTR)
			drvFail("The Chooser returned something other than a string.",
				NULL, true);

		//
		// Now write the chosen driver's ProgID into our registry area.
		// This creates the registry area if needed.
		//
		if(SysStringLen(vRes.bstrVal) > 0)						// Chooser dialog not cancelled
		{
			cp = uni_to_ansi(vRes.bstrVal);						// Get ProgID in ANSI
			strcpy(_szDriverID, cp);
			delete[] cp;

			save_driverid(_szDriverID);

			pChsrDsp->Release();
			SysFreeString(bsProgID);
		}
	}
	__except(EXCEPTION_EXECUTE_HANDLER)
		{
			if(pChsrDsp != NULL) pChsrDsp->Release();
			if(cp != NULL) delete[] cp;
			iRes = -1;
		}

	return(iRes);
}

//
// Dang it, an afterthought.
//
void SaveDriverID(char *id)
{
	save_driverid(id);
}


// ===============
// LOCAL UTILITIES
// ===============

// --------------
// get_driverid()
// --------------
//
// Retrieve the saved ASCOM driver ID from the registry. If there is no saved
// driver ID, the results differ based on the forConfig parameter. If it is false
// (trying to connect to the telescope/mount), then it is an error condition. 
// If forConfig is true (just getting this for Chooser initialization), just
// return an empty string if there is no saved ProgID.
//
static void get_driverid(char *id, bool forConfig)
{
	HKEY hKey;
	DWORD dwType;
	DWORD dwSize;

	//
	// Retrieve the ProgID of the driver we're to use.
	//
	if(RegOpenKeyEx(OUR_REGISTRY_BASE, 
			OUR_REGISTRY_AREA,
			0,
			KEY_READ,
			&hKey) != ERROR_SUCCESS)
	{
		if (forConfig)
		{
			id[0] = '\0';
			return;												// RETURN with empty ID
		}
		else
			drvFail(
				"You have not yet configured your telescope type and settings.",
				NULL, true);
	}

	dwSize = 256;
	if(RegQueryValueEx(hKey, 
			   OUR_DRIVER_SEL,
			   NULL,
			   &dwType,
			   (BYTE *)id,
			   &dwSize) != ERROR_SUCCESS)
	{
		if (forConfig)
		{
			id[0] = '\0';
		}
		else
		{
			RegCloseKey(hKey);
			drvFail(
				"Failed to read the driver ID from the registry.",
				NULL, true);
		}
	}
	RegCloseKey(hKey);
}

//
//
//
static void save_driverid(char *id)
{
	HKEY hKey;
	DWORD dwDisp;

	if(RegCreateKeyEx(OUR_REGISTRY_BASE, 
			  OUR_REGISTRY_AREA,
			  NULL, NULL, 0,
			  KEY_WRITE,
			  NULL,
			  &hKey,
			  &dwDisp) != ERROR_SUCCESS)
		drvFail("Failed to create or open the plug-in's registry area.",
			NULL, true);

	if(RegSetValueEx(hKey, 
			 OUR_DRIVER_SEL, 
			 0,
			 REG_SZ, 
			 (BYTE *)id, 
			 (strlen(id) + 1)) != ERROR_SUCCESS)
		drvFail("Failed to store the driver name into the registry.",
			NULL, true);

}

// -------------
// get_integer()
// -------------
//
// Get a named integer property. If the property is not supported
// then this raises a NOTIMPL exception and lets upstream code
// decide what to do.
//
static int get_integer(OLECHAR *name)
{
	DISPID dispid;
	VARIANT result;
	DISPPARAMS dispparms;
	EXCEPINFO excep;
	char *cp;
	char buf[256];

#ifdef CROSS_THREAD
	switchThreadIf();
#endif

	//
	// Get our dispatch ID
	//
	if(FAILED(_p_DrvDisp->GetIDsOfNames(
		IID_NULL, 
		&name, 
		1, 
		LOCALE_USER_DEFAULT,
		&dispid)))
	{
		cp = uni_to_ansi(name);
		wsprintf(buf, 
			 "The selected telescope driver is missing the %s property.", cp);
		delete[] cp;
		drvFail(buf, NULL, true);
	}

	//
	// No dispatch parameters for propget
	//
	dispparms.cArgs = 0;
	dispparms.rgvarg = NULL;
	dispparms.cNamedArgs = 0;
	dispparms.rgdispidNamedArgs = NULL;
	//
	// Invoke the method
	//
	if(FAILED(_p_DrvDisp->Invoke(dispid, 
				     IID_NULL, 
				     LOCALE_USER_DEFAULT, 
				     DISPATCH_PROPERTYGET,
				     &dispparms, 
				     &result, 
				     &excep, 
				     NULL)))
	{
		if(excep.scode == EXCEP_NOTIMPL)						// Optional
			NOTIMPL;											// Resignal silently
		else
		{
			cp = uni_to_ansi(name);
			wsprintf(buf, 
				 "Internal error reading from the %s property.", cp);
			delete[] cp;
			drvFail(buf, &excep, true);
		}
	}

	return(result.intVal);										// Return integer result
}

// ------------
// get_double()
// ------------
//
// Get a named double property. If the property is not supported
// then this raises a NOTIMPL exception and lets upstream code
// decide what to do.
//
static double get_double(OLECHAR *name)
{
	DISPID dispid;
	VARIANT result;
	DISPPARAMS dispparms;
	EXCEPINFO excep;
	char *cp;
	char buf[256];

#ifdef CROSS_THREAD
	switchThreadIf();
#endif

	//
	// Get our dispatch ID
	//
	if(FAILED(_p_DrvDisp->GetIDsOfNames(
		IID_NULL, 
		&name, 
		1, 
		LOCALE_USER_DEFAULT,
		&dispid)))
	{
		cp = uni_to_ansi(name);
		wsprintf(buf, 
			 "The selected telescope driver is missing the %s property.", cp);
		delete[] cp;
		drvFail(buf, NULL, true);
	}

	//
	// No dispatch parameters for propget
	//
	dispparms.cArgs = 0;
	dispparms.rgvarg = NULL;
	dispparms.cNamedArgs = 0;
	dispparms.rgdispidNamedArgs = NULL;
	//
	// Invoke the method
	//
	if(FAILED(_p_DrvDisp->Invoke(dispid, 
				     IID_NULL, 
				     LOCALE_USER_DEFAULT, 
				     DISPATCH_PROPERTYGET,
				     &dispparms, 
				     &result, 
				     &excep, 
				     NULL)))
	{
		if(excep.scode == EXCEP_NOTIMPL)						// Optional
			NOTIMPL;											// Resignal silently
		else
		{
			cp = uni_to_ansi(name);
			wsprintf(buf, 
				 "Internal error reading from the %s property.", cp);
			delete[] cp;
			drvFail(buf, &excep, true);
		}
	}

	return(result.dblVal);										// Return long result
}

// ------------
// set_double()
// ------------
//
// Set a named double property. If the property is not supported
// then this raises a NOTIMPL exception and lets upstream code
// decide what to do.
//
static void set_double(OLECHAR *name, double val)
{
	DISPID dispid;
	VARIANTARG rgvarg[1];
	VARIANT result;
	DISPID ppdispid[1];
	DISPPARAMS dispparms;
	EXCEPINFO excep;
	char *cp;
	char buf[256];

#ifdef CROSS_THREAD
	switchThreadIf();
#endif

	//
	// Get our dispatch ID
	//
	if(FAILED(_p_DrvDisp->GetIDsOfNames(
		IID_NULL, 
		&name, 
		1, 
		LOCALE_USER_DEFAULT,
		&dispid)))
	{
		cp = uni_to_ansi(name);
		wsprintf(buf, 
			 "The selected telescope driver is missing the %s property.", cp);
		delete[] cp;
		drvFail(buf, NULL, true);
	}

	//
	// Special setup for propput (See MSKB Q175618)
	//
	rgvarg[0].vt = VT_R8;
	rgvarg[0].dblVal = val;
	dispparms.cArgs = 1;
	dispparms.rgvarg = rgvarg;
	ppdispid[0] = DISPID_PROPERTYPUT;
	dispparms.cNamedArgs = 1;
	dispparms.rgdispidNamedArgs = ppdispid;
	//
	// Invoke the method
	//
	if(FAILED(_p_DrvDisp->Invoke(dispid, 
				     IID_NULL, 
				     LOCALE_USER_DEFAULT, 
				     DISPATCH_PROPERTYPUT,
				     &dispparms, 
				     &result, 
				     &excep, 
				     NULL)))
	{
		if(excep.scode == EXCEP_NOTIMPL)						// Optional
			NOTIMPL;											// Resignal silently
		else
		{
			cp = uni_to_ansi(name);
			wsprintf(buf, 
				 "Internal error writing to the %s property.", cp);
			delete[] cp;
			drvFail(buf, &excep, true);
		}
	}
}

// ----------
// get_bool()
// ----------
//
// Get a named boolean property. If the property is not supported
// then this raises a NOTIMPL exception and lets upstream code
// decide what to do.
//
static bool get_bool(OLECHAR *name)
{
	DISPID dispid;
	VARIANT result;
	DISPPARAMS dispparms;
	EXCEPINFO excep;
	char *cp;
	char buf[256];

#ifdef CROSS_THREAD
	switchThreadIf();
#endif

	//
	// Get our dispatch ID
	//
	if(FAILED(_p_DrvDisp->GetIDsOfNames(
		IID_NULL, 
		&name, 
		1, 
		LOCALE_USER_DEFAULT,
		&dispid)))
	{
		cp = uni_to_ansi(name);
		wsprintf(buf, 
			 "The selected telescope driver is missing the %s property.", cp);
		delete[] cp;
		drvFail(buf, NULL, true);
	}

	//
	// No dispatch parameters for propget
	//
	dispparms.cArgs = 0;
	dispparms.rgvarg = NULL;
	dispparms.cNamedArgs = 0;
	dispparms.rgdispidNamedArgs = NULL;
	//
	// Invoke the method
	//
	if(FAILED(_p_DrvDisp->Invoke(dispid, 
				     IID_NULL, 
				     LOCALE_USER_DEFAULT, 
				     DISPATCH_PROPERTYGET,
				     &dispparms, 
				     &result, 
				     &excep, 
				     NULL)))
	{
		if(excep.scode == EXCEP_NOTIMPL)						// Optional
			NOTIMPL;											// Resignal silently
		else
		{
			cp = uni_to_ansi(name);
			wsprintf(buf, 
				 "Internal error reading from the %s property.", cp);
			delete[] cp;
			drvFail(buf, &excep, true);
		}
	}

	return(result.boolVal == VARIANT_TRUE);						// Return C++ bool result
}


// ----------
// set_bool()
// ----------
//
// Set a named boolean property. If the property is not supported
// then this raises a NOTIMPL exception and lets upstream code
// decide what to do.
//
static void set_bool(OLECHAR *name, bool val)
{
	DISPID dispid;
	VARIANTARG rgvarg[1];
	VARIANT result;
	DISPID ppdispid[1];
	DISPPARAMS dispparms;
	EXCEPINFO excep;
	char *cp;
	char buf[256];
	HRESULT hr;

#ifdef CROSS_THREAD
	switchThreadIf();
#endif

	//
	// Get our dispatch ID
	//
	if(FAILED(hr = _p_DrvDisp->GetIDsOfNames(
		IID_NULL, 
		&name, 
		1, 
		LOCALE_USER_DEFAULT,
		&dispid)))
	{
		cp = uni_to_ansi(name);
		wsprintf(buf, 
			 "The selected telescope driver is missing the %s property.", cp);
		delete[] cp;
		drvFail(buf, NULL, true);
	}

	rgvarg[0].vt = VT_BOOL;
	rgvarg[0].boolVal = (val ? VARIANT_TRUE : VARIANT_FALSE);	// Translate to Variant Bool
	dispparms.cArgs = 1;
	dispparms.rgvarg = rgvarg;
	ppdispid[0] = DISPID_PROPERTYPUT;
	dispparms.cNamedArgs = 1;									// PropPut kludge
	dispparms.rgdispidNamedArgs = ppdispid;
	if(FAILED(_p_DrvDisp->Invoke(
			dispid,
			IID_NULL, 
			LOCALE_USER_DEFAULT, 
			DISPATCH_PROPERTYPUT, 
			&dispparms, 
			&result,
			&excep, NULL)))
	{
		if(excep.scode == EXCEP_NOTIMPL)						// Optional
			NOTIMPL;											// Resignal silently
		else
		{
			cp = uni_to_ansi(name);
			wsprintf(buf, 
				 "Internal error writing to the %s property.", cp);
			delete[] cp;
			drvFail(buf, &excep, true);
		}
	}

}

// ------------
// get_string()
// ------------
//
// Returns -> to new[]'ed string. Caller must release.
//
static char *get_string(OLECHAR *name )
{
	DISPID dispid;
	DISPPARAMS dispparms;
	EXCEPINFO excep;
	VARIANT vRes;
	char *cp;
	char buf[256];

#ifdef CROSS_THREAD
	switchThreadIf();
#endif

	//
	// Get our dispatch ID
	//
	if(FAILED(_p_DrvDisp->GetIDsOfNames(
		IID_NULL, 
		&name,
		1, 
		LOCALE_USER_DEFAULT,
		&dispid)))
	{
		cp = uni_to_ansi(name);
		wsprintf(buf, 
			 "The selected telescope driver is missing the %s property.", cp);
		delete[] cp;
		drvFail(buf, &excep, true);
	}

	//
	// No dispatch parameters for propget
	//
	dispparms.cArgs = 0;
	dispparms.rgvarg = NULL;
	dispparms.cNamedArgs = 0;
	dispparms.rgdispidNamedArgs = NULL;
	//
	// Invoke the method
	//
	if(FAILED(_p_DrvDisp->Invoke(dispid, 
				     IID_NULL, 
				     LOCALE_USER_DEFAULT, 
				     DISPATCH_PROPERTYGET,
				     &dispparms, 
				     &vRes, 
				     &excep, 
				     NULL)))
	{
		if(excep.scode == EXCEP_NOTIMPL)						// Optional
			NOTIMPL;											// Resignal silently
		else
		{
			cp = uni_to_ansi(name);
			wsprintf(buf, 
				 "Internal error reading from the %s property.", cp);
			delete[] cp;
			drvFail(buf, &excep, true);
		}
	}
		
	return(uni_to_ansi(vRes.bstrVal));
}


#ifdef CROSS_THREAD
//
// Gets the IDispatch interface on a new thread from a previously obtained
// marshal stream, then gets a new interface marshaling stream on the new
// thread (remembering the thread on which the interface is currently
// marshaled). This may be repeatedly called to wsitch the IDispatch to
// the scope from one thread to another. 
//
static void switchThreadIf()
{
	if (GetCurrentThreadId() != dCurrIntfcThreadId)
	{
		IDispatch *pTemp;

		dCurrIntfcThreadId = GetCurrentThreadId();
		if(FAILED(_p_GIT->GetInterfaceFromGlobal(dwIntfcCookie, 
						IID_IDispatch, 
						(void **)&pTemp)))
			drvFail("Failed to get interface from GIT in new thread", NULL, true);
		_p_DrvDisp = pTemp;
	}
}
#endif