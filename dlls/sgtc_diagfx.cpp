// build-11 effect diagnostics: label every candidate spark emitter behind
// sgtc_diag_log so an idle bot run can attribute spurious bullet/spark effects.
#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "sgtc_diag.h"

static int s_iSgtcSparkLog = 0;

void SGTC_DiagSparks( const char *pszSource, const Vector &vecOrigin )
{
	if( !SGTC_DiagEnabled() )
		return;
	if( s_iSgtcSparkLog > 5000 )
	{
		if( s_iSgtcSparkLog == 5001 )
		{
			++s_iSgtcSparkLog;
			UTIL_LogPrintf( "SGTC_DIAG {\"cmd\":\"spark\",\"src\":\"limit\"}\n" );
		}
		return;
	}
	++s_iSgtcSparkLog;
	UTIL_LogPrintf( "SGTC_DIAG {\"cmd\":\"spark\",\"src\":\"%s\",\"origin\":[%.1f,%.1f,%.1f]}\n",
		pszSource ? pszSource : "?", vecOrigin.x, vecOrigin.y, vecOrigin.z );
}
