//#include "stdafx.h"
#include <iostream>
#include <string>
#include <filesystem>
#include <memory>
#include <fstream>
#include <laszip_api.h>

namespace fs = std::experimental::filesystem::v1;

static void showUsage(const std::string &name);
static uint8_t getLasFiles(const std::string &directory, std::vector<std::string> &filelist);
static void laszipError(laszip_POINTER laszip);
static uint8_t readLazFile(const std::string &filename);

int main(int argc, char* argv[])
{
	std::string dirpath = argc > 1 ? argv[1] : "";
	std::string execname = argv[0];
	std::vector<std::string> fslist;
	std::vector<std::string> corrupt_or_missing;
	std::vector<std::string> misclassified;
	bool foundcorrupt = false;
	bool foundmisclass = false;

	if (laszip_load_dll())
	{
		std::cerr << "ERROR: Loading laszip dll" << std::endl;
		exit(1);
	}

	if (argc < 2)
	{
		showUsage(execname);
		return 1;
	}

	uint8_t parse_result = getLasFiles(dirpath, fslist);
	if (parse_result != 0)
	{
		showUsage(execname);
		return 1;
	}

	while (!fslist.empty())
	{
		std::string lazfile = fslist.back();
		uint8_t lazreader_result = readLazFile(lazfile);

		if (lazreader_result == 2)
		{
			std::cout << "------LASZIP ERROR-------- corrupt or invalid file" << std::endl;
			corrupt_or_missing.push_back((lazfile + " (Corrupt)"));
			foundcorrupt = true;
		}
		if (lazreader_result == 1)
		{
			std::cout << "File has points that aren't class 2: " << lazfile << std::endl;
			misclassified.push_back(lazfile);
			foundmisclass = true;
		}

		fslist.pop_back();
	}

	if (foundcorrupt)
	{
		std::string oname = "\\corrupt_and_missing_laz.txt";
		std::string outlist = dirpath + oname;
		std::ofstream outstream(outlist, std::ios::binary);

		std::cout << "Found corrupt or missing .laz, writing to " << outlist << std::endl;
		while (!corrupt_or_missing.empty())
		{
			outstream << corrupt_or_missing.back() << "\r\n";
			corrupt_or_missing.pop_back();
		}

		outstream.close();
	}

	if (foundmisclass)
	{
		std::string batfile = dirpath + "\\setup_reclassify.bat";
		std::string dirname = dirpath + "\\orig";
		fs::path origdir(dirname);
		std::ofstream outstream(batfile, std::ios::binary);

		outstream << "placeholder" << "\r\n";
		outstream.close();

		fs::create_directory(origdir);
		while (!misclassified.empty())
		{
			fs::path lazpath(misclassified.back());
			fs::path copyto(dirname);
			copyto.append(lazpath.filename());

			fs::copy_file(lazpath, copyto);
			fs::remove(lazpath);
			misclassified.pop_back();
		}
	}

	return 0;
}

static void showUsage(const std::string &name)
{
	std::cerr << "Usage : " << name << " [base directory]" << std::endl;
}

//find laz/las files in all subdirectories underneath base directory
static uint8_t getLasFiles(const std::string &directory, std::vector<std::string> &fileslist)
{
	std::vector<std::string> dirskiplist = { "log", "logs", ".log" };
	std::vector<std::string> filekeeplist = { ".las", ".laz" };
	fs::path directorypath(directory);

	if (!(fs::exists(directorypath) && fs::is_directory(directorypath)))
	{
		return 1;
	}

	try
	{
		//this convenience class is really nice  (>'-')> <('-'<) ^('-')^ v('-')v (>'-')> (^-^) 
		fs::recursive_directory_iterator directoryiter(directorypath);
		fs::recursive_directory_iterator end;
		while (directoryiter != end)
		{
			//don't recurse through all the directories containing log files
			if (fs::is_directory(directoryiter->path()) &&
				(std::find(dirskiplist.begin(), dirskiplist.end(), directoryiter->path().filename()) != dirskiplist.end()))
			{
				directoryiter.disable_recursion_pending();
			}
			else
			{
				//if the extension is las or laz, add it to the list of results
				if (std::find(filekeeplist.begin(), filekeeplist.end(), directoryiter->path().extension()) != filekeeplist.end())
				{
					fileslist.push_back(directoryiter->path().string());
				}
			}
			std::error_code ec;
			directoryiter.increment(ec);
			if (ec)
			{
				std::cerr << "Error while accessing : " << directoryiter->path().string() << " : " << ec.message() << std::endl;
				return 1;
			}
		}
	}
	catch (std::system_error & e)
	{
		std::cerr << "Received Exception : " << e.what() << std::endl;
		return 1;
	}

	return 0;
}

static void laszipError(laszip_POINTER laszip)
{
	if (laszip)
	{
		laszip_CHAR* lzerror;
		if (laszip_get_error(laszip, &lzerror))
		{
			std::cerr << "Getting error messages" << std::endl;
		}
		std::cerr << "DLL Error message: " << lzerror << std::endl;
	}
}

static uint8_t readLazFile(const std::string &filename)
{
	//create reader
	laszip_POINTER laszip_reader;
	laszip_BOOL is_compressed = 0;
	laszip_header* lzheader;
	laszip_point* lzpoints;
	std::vector<char> cfilename(filename.c_str(), filename.c_str() + filename.size() + 1u);

	if (laszip_create(&laszip_reader))
	{
		std::cerr << "DLL ERROR: creating laszip reader" << std::endl;
		laszipError(laszip_reader);
		return 2;
	}

	//open reader
	if (laszip_open_reader(laszip_reader, &cfilename[0], &is_compressed))
	{
		std::cerr << "DLL ERROR: opening laszip reader" << std::endl;
		laszipError(laszip_reader);
		return 2;
	}

	//get a pointer to header
	if (laszip_get_header_pointer(laszip_reader, &lzheader))
	{
		std::cerr << "DLL ERROR: getting header from laszip reader" << std::endl;
		laszipError(laszip_reader);
		return 2;
	}

	//get a pointer to the points to read
	if (laszip_get_point_pointer(laszip_reader, &lzpoints))
	{
		std::cerr << "DLL ERROR: getting point pointer from laszip reader" << std::endl;
		laszipError(laszip_reader);
		return 2;
	}

	//read the points
	laszip_I64 numberofpoints = (lzheader->number_of_point_records ? lzheader->number_of_point_records : lzheader->extended_number_of_point_records);
	laszip_I64 pointcount = 0;
	while (pointcount < numberofpoints)
	{
		if (laszip_read_point(laszip_reader))
		{
			std::cerr << "DLL ERROR: reading point from laszip reader" << std::endl;
			laszipError(laszip_reader);
			return 2;
		}
		uint16_t pointclass = lzpoints->classification;
		if (pointclass != 2)
		{
			//close the reader
			if (laszip_close_reader(laszip_reader))
			{
				std::cerr << "DLL ERROR: closing laszip reader" << std::endl;
				laszipError(laszip_reader);
				return 2;
			}

			//destroy the reader
			if (laszip_destroy(laszip_reader))
			{
				std::cerr << "DLL ERROR: destroying laszip reader" << std::endl;
				laszipError(laszip_reader);
				return 2;
			}
			return 1;
		}
		pointcount++;
	}

	//close the reader
	if (laszip_close_reader(laszip_reader))
	{
		std::cerr << "DLL ERROR: closing laszip reader" << std::endl;
		laszipError(laszip_reader);
		return 2;
	}

	//destroy the reader
	if (laszip_destroy(laszip_reader))
	{
		std::cerr << "DLL ERROR: destroying laszip reader" << std::endl;
		laszipError(laszip_reader);
		return 2;
	}

	return 0;
}