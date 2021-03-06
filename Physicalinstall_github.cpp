/*
 *    _____      _ ____  ____
 *   / ___/_____(_) __ \/ __ )
 *   \__ \/ ___/ / / / / __  |
 *  ___/ / /__/ / /_/ / /_/ / 
 * /____/\___/_/_____/_____/  
 *
 *
 * BEGIN_COPYRIGHT
 *
 * This file is part of SciDB.
 * Copyright (C) 2008-2014 SciDB, Inc.
 *
 * SciDB is free software: you can redistribute it and/or modify
 * it under the terms of the AFFERO GNU General Public License as published by
 * the Free Software Foundation.
 *
 * SciDB is distributed "AS-IS" AND WITHOUT ANY WARRANTY OF ANY KIND,
 * INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY,
 * NON-INFRINGEMENT, OR FITNESS FOR A PARTICULAR PURPOSE. See
 * the AFFERO GNU General Public License for the complete license terms.
 *
 * You should have received a copy of the AFFERO GNU General Public License
 * along with SciDB.  If not, see <http://www.gnu.org/licenses/agpl-3.0.html>
 *
 * END_COPYRIGHT
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "query/Operator.h"

#if SCIDB_VARIANT < 1412
#include "query/Network.h"
#else
#include "util/Network.h"
#endif

#define CMDBUFSZ 16384

#ifdef CPP11
using namespace std;
#else
using namespace boost;
#endif

namespace scidb
{

class Physicalinstall_github: public PhysicalOperator
{
public:
    Physicalinstall_github(string const& logicalName,
                           string const& physicalName,
                           Parameters const& parameters,
                           ArrayDesc const& schema):
        PhysicalOperator(logicalName, physicalName, parameters, schema)
    {
    }

    shared_ptr<Array> execute(vector<shared_ptr<Array> >& inputArrays,
            shared_ptr<Query> query)
    {
        char cmd[CMDBUFSZ];
        char dir[4096];
        int k;
        char *d;

        if (query->getInstanceID() == 0)
        {
            Instances instances;
            SystemCatalog::getInstance()->getInstances(instances);
            Instances::const_iterator iter = instances.begin();
            InstanceID id;

            std::string const repo = ((shared_ptr<OperatorParamPhysicalExpression>&)_parameters[0])->getExpression()->evaluate().getString();
            std::string branch;
            if(_parameters.size()>1)
                branch = ((shared_ptr<OperatorParamPhysicalExpression>&)_parameters[1])->getExpression()->evaluate().getString();
            else branch = "master";

            std::string options;
            if(_parameters.size()>2)
                options = ((shared_ptr<OperatorParamPhysicalExpression>&)_parameters[2])->getExpression()->evaluate().getString();
            else options = "";

            snprintf(dir,4096,"/tmp/install_github_XXXXXX");
            d = mkdtemp(dir);
            if(!d) throw SYSTEM_EXCEPTION(SCIDB_SE_OPERATOR, SCIDB_LE_ILLEGAL_OPERATION)
                        << "failed to create temp directory";
            snprintf(cmd,CMDBUFSZ,"cd %s && %s wget https://github.com/%s/archive/%s.tar.gz",dir,options.c_str(),repo.c_str(),branch.c_str());
std::fprintf(stderr, "cmd %s\n",cmd);
            k = ::system((const char *)cmd);
            if(k!=0) throw SYSTEM_EXCEPTION(SCIDB_SE_OPERATOR, SCIDB_LE_ILLEGAL_OPERATION)
                        << "failed to retrieve repository";

// Get our base SciDB path (XXX is there an easier way?), and build the plugin
            memset(cmd,0,CMDBUFSZ);
            pid_t mypid = getpid();
            std::string branch_dir_suffix = branch;
            if (branch[0] == 'v') branch_dir_suffix = branch.substr(1);
            snprintf(cmd,CMDBUFSZ,"x=`readlink /proc/%d/exe`;x=`dirname $x`;x=`dirname $x`;cd %s; tar -zxf *;cd %s/*-%s;SCIDB=$x %s make && tar -zcf ../plugin.tar.gz *.so",mypid,dir,dir,branch_dir_suffix.c_str(), options.c_str());
            k = ::system((const char *)cmd);
            if(k!=0) throw SYSTEM_EXCEPTION(SCIDB_SE_OPERATOR, SCIDB_LE_ILLEGAL_OPERATION)
                        << "failed to build plugin";

// Read the compiled plugin into a buffer for distribution
            memset(cmd,0,CMDBUFSZ);
            snprintf(cmd, CMDBUFSZ, "%s/plugin.tar.gz", dir);
            struct stat sb;
            if (stat(cmd, &sb) == -1) throw SYSTEM_EXCEPTION(SCIDB_SE_OPERATOR, SCIDB_LE_ILLEGAL_OPERATION)
                        << "failed to stat plugin";
            shared_ptr<SharedBuffer> tarball(new MemoryBuffer(NULL, sb.st_size * sizeof(char)));
            shared_ptr<SharedBuffer> nothing(new MemoryBuffer(NULL, 0));
            FILE *f = std::fopen(cmd, "r");
            if (!f) throw SYSTEM_EXCEPTION(SCIDB_SE_OPERATOR, SCIDB_LE_ILLEGAL_OPERATION)
                        << "failed to open plugin";
            size_t sz = std::fread((char *)tarball->getData(), sizeof(char), sb.st_size, f);
            std::fclose(f);
            if(sz != sb.st_size*sizeof(char)) throw SYSTEM_EXCEPTION(SCIDB_SE_OPERATOR, SCIDB_LE_ILLEGAL_OPERATION)
                        << "failed to read plugin";

// Install the plugin locally
            memset(cmd,0,CMDBUFSZ);
            snprintf(cmd,CMDBUFSZ,"x=`readlink /proc/%d/exe`;x=`dirname $x`;x=`dirname $x`;x=$x/lib/scidb/plugins/;cd %s;tar -C $x -zxf plugin.tar.gz;cd ..;rm -rf %s",mypid,dir,dir);
            k = ::system((const char *)cmd);
            if(k!=0) throw SYSTEM_EXCEPTION(SCIDB_SE_OPERATOR, SCIDB_LE_ILLEGAL_OPERATION)
                        << "failed to install plugin";

// Copy the plugin to other instances and install it
std::fprintf(stderr,"COPY PHASE\n");
            iter = instances.begin();
            id = 0;
            const char *s = "";
            while(iter != instances.end())
            {
std::fprintf(stderr, "id=%d Id=%d host=%s\n", (int)id, (int)iter->getInstanceId(), iter->getHost().c_str());
              if(iter->getInstanceId() != query->getInstanceID())
              {
                if(strcmp(s, iter->getHost().c_str()) != 0)
                {
std::fprintf(stderr,"copy to %d\n", (int)iter->getInstanceId());
                  // copy file
                  s = iter->getHost().c_str();
                  BufSend(id, tarball, query);
                } else
                {
std::fprintf(stderr,"NO copy to %d\n", (int)iter->getInstanceId());
                    // don't copy
                    BufSend(id, nothing, query);
                }
              }
              ++id;
              ++iter;
            }

            shared_ptr<Array> outputArray(new MemArray(_schema, query));
            shared_ptr<ArrayIterator> outputArrayIter = outputArray->getIterator(0);
            Coordinates position(1, 0);
            shared_ptr<ChunkIterator> outputChunkIter = outputArrayIter->newChunk(position).getIterator(query, 0);
            outputChunkIter->setPosition(position);
            Value value;
            value.setBool(true);
            outputChunkIter->writeItem(value);
            outputChunkIter->flush();
            return outputArray;
        }

// Non-coordinator instances...
        shared_ptr<SharedBuffer> buf = BufReceive(query->getCoordinatorID(), query);
        if(buf)
        {
            Instances instances;
            SystemCatalog::getInstance()->getInstances(instances);
            snprintf(dir,4096,"/tmp/install_github_XXXXXX");
            d = mkdtemp(dir);
            if(!d) throw SYSTEM_EXCEPTION(SCIDB_SE_OPERATOR, SCIDB_LE_ILLEGAL_OPERATION)
                        << "failed to create temp directory";
            memset(cmd,0,CMDBUFSZ);
            snprintf(cmd, CMDBUFSZ, "%s/plugin.tar.gz", dir);
            FILE *f = std::fopen(cmd, "w+");
            if (!f) throw SYSTEM_EXCEPTION(SCIDB_SE_OPERATOR, SCIDB_LE_ILLEGAL_OPERATION)
                        << "failed to open plugin";
            size_t sz = std::fwrite((char *)buf->getData(), sizeof(char), buf->getSize(), f);
            std::fclose(f);
            buf->free();
            if(sz != buf->getSize()*sizeof(char)) throw SYSTEM_EXCEPTION(SCIDB_SE_OPERATOR, SCIDB_LE_ILLEGAL_OPERATION)
                        << "failed to write plugin";
// Install the plugin locally
            memset(cmd,0,CMDBUFSZ);
            pid_t mypid = getpid();
            snprintf(cmd,CMDBUFSZ,"x=`readlink /proc/%d/exe`;x=`dirname $x`;x=`dirname $x`;x=$x/lib/scidb/plugins/;cd %s;tar -C $x -zxf plugin.tar.gz;cd ..; rm -rf %s",mypid,dir,dir);
            k = ::system((const char *)cmd);
            if(k!=0) throw SYSTEM_EXCEPTION(SCIDB_SE_OPERATOR, SCIDB_LE_ILLEGAL_OPERATION)
                        << "failed to install plugin";

        }
        return shared_ptr<Array>(new MemArray(_schema,query));
    }
};

REGISTER_PHYSICAL_OPERATOR_FACTORY(Physicalinstall_github, "install_github", "Physicalinstall_github");

} //namespace
