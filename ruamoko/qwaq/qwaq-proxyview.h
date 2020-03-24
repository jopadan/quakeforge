#ifndef __qwaq_proxyview_h
#define __qwaq_proxyview_h

#include "qwaq-view.h"

@interface ProxyView : Object
{
	View       *view;
}
-initWithView:(View *) view;
-setView: (View *) view;
@end

@interface ProxyView (View) <View, TextContext>
@end

#endif//__qwaq_proxyview_h
