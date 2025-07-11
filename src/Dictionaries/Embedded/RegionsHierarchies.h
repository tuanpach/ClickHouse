#pragma once

#include <unordered_map>
#include <Poco/Exception.h>
#include <Dictionaries/Embedded/GeodataProviders/IHierarchiesProvider.h>
#include <Dictionaries/Embedded/RegionsHierarchy.h>

namespace DB
{

/** Contains several hierarchies of regions.
  * Used to support several different perspectives on the ownership of regions by countries.
  * First of all, for the Falklands/Malvinas (UK and Argentina points of view).
  */
class RegionsHierarchies
{
private:
    using Container = std::unordered_map<std::string, RegionsHierarchy>;
    Container data;

public:
    explicit RegionsHierarchies(IRegionsHierarchiesDataProviderPtr data_provider);

    /** Reloads, if necessary, all hierarchies of regions.
      */
    void reload()
    {
        for (auto & elem : data)
            elem.second.reload();
    }

    const RegionsHierarchy & get(const std::string & key) const
    {
        auto it = data.find(key);

        if (data.end() == it)
            throw Poco::Exception("There is no regions hierarchy for key " + key);

        return it->second;
    }
};

}
