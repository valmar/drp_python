#include <unistd.h>
#include <string>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include "xtcdata/xtc/TypeId.hh"
#include "xtcdata/xtc/XtcFileIterator.hh"
#include "xtcdata/xtc/XtcIterator.hh"
#include "xtcdata/xtc/TransitionId.hh"

int main(int argc, char *argv[])
{

    // if (argc != 2)
    // {
    //     printf("Usage: drp_xtc_server <xtc file>\n");
    //     exit(1);
    // }

    std::string xtc_filename = "/cds/data/psdm/tmo/tmox49720/xtc/"
                               "tmox49720-r0207-s003-c000.xtc2";

    std::string output_filename = "./test_multirun.xtc2";

    int ofd = open(
        output_filename.c_str(),
        O_WRONLY | O_CREAT,
        S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH);
    if (ofd < 0)
    {
        std::cerr << "Unable to open file " << output_filename << std::endl;
        exit(2);
    }

    int xtc_fd = open(xtc_filename.c_str(), O_RDONLY);
    if (xtc_fd < 0)
    {
        std::cerr << "Error in opening the file: " << xtc_filename
                  << std::endl;
        return 0;
    }

    XtcData::XtcFileIterator iter(xtc_fd, 0x4000000);

    unsigned int count_l1accept = 0;

    XtcData::Dgram *dg;

    while ((dg = iter.next()))
    {

        if (dg->service() == 12)
        {
            count_l1accept += 1;
            if (count_l1accept > 60)
            {
                continue;
            }
        }

        if (dg->service() == 10)
        {
            if (count_l1accept > 60)
            {
                continue;
            }
        }

        if (write(ofd, dg, sizeof(*dg) + dg->xtc.sizeofPayload()) < 0)
        {
            std::cerr << "Error writing to output xtc file." << std::endl;
            return 0;
        }
    }

    close(xtc_fd);
    std::cout << "Closed " << xtc_filename << " file" << std::endl;

    XtcData::TypeId tid(XtcData::TypeId::Parent, 0);
    uint32_t env = 0;
    struct timeval tv;
    tv.tv_sec = 1013704200;
    tv.tv_usec = 0;

    XtcData::Transition tr(XtcData::Dgram::Event, XtcData::TransitionId::Unconfigure, XtcData::TimeStamp(tv.tv_sec, tv.tv_usec), env);
    XtcData::Dgram unconfigure_dg(tr, XtcData::Xtc(tid));

    if (write(ofd, &unconfigure_dg, sizeof(unconfigure_dg) + unconfigure_dg.xtc.sizeofPayload()) < 0)
    {
        std::cerr << "Error writing to output xtc file." << std::endl;
        return 0;
    }

    xtc_filename = "/cds/data/psdm/tmo/tmox49720/xtc/"
                   "tmox49720-r0208-s003-c000.xtc2";

    xtc_fd = open(xtc_filename.c_str(), O_RDONLY);
    if (xtc_fd < 0)
    {
        std::cerr << "Error in opening the file: " << xtc_filename
                  << std::endl;
        return 0;
    }

    XtcData::XtcFileIterator iter2(xtc_fd, 0x4000000);

    count_l1accept = 0;

    while ((dg = iter2.next()))
    {

        if (dg->service() == 12)
        {
            count_l1accept += 1;
            if (count_l1accept > 60)
            {
                continue;
            }
        }

        if (dg->service() == 10)
        {
            if (count_l1accept > 60)
            {
                continue;
            }
        }

        if (write(ofd, dg, sizeof(*dg) + dg->xtc.sizeofPayload()) < 0)
        {
            std::cerr << "Error writing to output xtc file." << std::endl;
            return 0;
        }
    }
    close(xtc_fd);
    std::cout << "Closed " << xtc_filename << " file" << std::endl;
    close(ofd);

    return 0;
}
