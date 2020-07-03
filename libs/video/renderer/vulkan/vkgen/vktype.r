#include <hash.h>

#include "vkalias.h"
#include "vkenum.h"
#include "vkgen.h"
#include "vkstruct.h"
#include "vktype.h"

@implementation Type

static hashtab_t *registered_types;

static string get_type_key (void *type, void *unused)
{
	return ((Type *) type).type.encoding;
}

+(void)initialize
{
	registered_types = Hash_NewTable (127, get_type_key, nil, nil);
}

-initWithType: (qfot_type_t *) type
{
	if (!(self = [super init])) {
		return nil;
	}
	self.type = type;
	Hash_Add (registered_types, self);
	return self;
}

+(Type *) findType: (qfot_type_t *) type
{
	if (type.meta == ty_alias && !type.alias.name) {
		type = type.alias.full_type;
	}
	return (Type *) Hash_Find (registered_types, type.encoding);
}

+fromType: (qfot_type_t *) type
{
	if (type.size == 0) {
		return nil;
	}
	switch (type.meta) {
		case ty_basic:
		case ty_array:
		case ty_class:
			return [[Type alloc] initWithType: type];
		case ty_enum:
			return [[Enum alloc] initWithType: type];
		case ty_struct:
		case ty_union:
			return [[Struct alloc] initWithType: type];
		case ty_alias:
			if (type.alias.name) {
				return [[Alias alloc] initWithType: type];
			}
			break;
	}
	return nil;
}

-(string) key
{
	return type.encoding;
}

-(string) name
{
	//FIXME extract alias name and return proper type name
	return type.encoding;
}

-(void) addToQueue
{
	string name = [self name];
	if (type.meta == ty_basic && type.type == ev_pointer) {
		[[Type findType: type.fldptr.aux_type] addToQueue];
	}
}

-(Type *) resolveType
{
	return self;
}

@end
