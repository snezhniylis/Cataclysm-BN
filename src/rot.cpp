#include "rot.h"

#include "item_location.h"
#include "map.h"
#include "vehicle.h"
#include "units.h"
#include "veh_type.h"
#include "vpart_position.h"

namespace rot
{
temperature_flag temperature_flag_for_location( const map &m, const item_location &loc )
{
    switch( loc.where() ) {
        case item_location::type::invalid:
            return temperature_flag::TEMP_NORMAL;
        case item_location::type::character:
            return temperature_flag::TEMP_NORMAL;
        case item_location::type::map: {
            tripoint pos = loc.position();
            if( m.has_flag_furn( TFLAG_FREEZER, pos ) ) {
                return temperature_flag::TEMP_FREEZER;
            }
            if( m.has_flag_furn( TFLAG_FRIDGE, pos ) ) {
                return temperature_flag::TEMP_FRIDGE;
            }
            if( m.ter( pos ) == t_rootcellar ) {
                return temperature_flag::TEMP_ROOT_CELLAR;
            }

            return temperature_flag::TEMP_NORMAL;
        }
        case item_location::type::vehicle: {
            tripoint pos = loc.position();
            optional_vpart_position veh = m.veh_at( pos );
            if( !veh ) {
                debugmsg( "Expected vehicle at %d, %d, %d, but couldn't find any", pos.x, pos.y, pos.z );
                return temperature_flag::TEMP_NORMAL;
            }
            int cargo_index = veh->vehicle().part_with_feature( veh->part_index(), VPFLAG_CARGO, true );
            if( cargo_index < 0 ) {
                debugmsg( "Expected cargo part at %d, %d, %d, but couldn't find any", pos.x, pos.y, pos.z );
                return temperature_flag::TEMP_NORMAL;
            }
            return temperature_flag_for_part( veh->vehicle(), cargo_index );
        }
        case item_location::type::container:
            return temperature_flag_for_location( m, loc.parent_item() );
        default:
            debugmsg( "Invalid item location %d", static_cast<int>( loc.where() ) );
            return temperature_flag::TEMP_NORMAL;

    }
}

temperature_flag temperature_flag_for_part( const vehicle &veh, size_t part_index )
{
    const vehicle_part &part = veh.cpart( part_index );
    if( !part.enabled ) {
        return temperature_flag::TEMP_NORMAL;
    }

    if( part.has_flag( VPFLAG_FREEZER ) ) {
        return temperature_flag::TEMP_FREEZER;
    }

    if( part.has_flag( VPFLAG_FRIDGE ) ) {
        return temperature_flag::TEMP_FRIDGE;
    }
    return temperature_flag::TEMP_NORMAL;
}

} // namespace rot
