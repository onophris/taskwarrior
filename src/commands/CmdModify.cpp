////////////////////////////////////////////////////////////////////////////////
// taskwarrior - a command line task list manager.
//
// Copyright 2006 - 2011, Paul Beckingham, Federico Hernandez.
// All rights reserved.
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation; either version 2 of the License, or (at your option) any later
// version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License along with
// this program; if not, write to the
//
//     Free Software Foundation, Inc.,
//     51 Franklin Street, Fifth Floor,
//     Boston, MA
//     02110-1301
//     USA
//
////////////////////////////////////////////////////////////////////////////////

#define L10N                                           // Localization complete.

#include <iostream>
#include <sstream>
#include <Context.h>
#include <Permission.h>
#include <main.h>
#include <text.h>
#include <i18n.h>
#include <CmdModify.h>

extern Context context;

////////////////////////////////////////////////////////////////////////////////
CmdModify::CmdModify ()
{
  _keyword     = "modify";
  _usage       = "task <filter> modify <modifications>";
  _description = std::string (STRING_CMD_MODIFY_USAGE1) +
                 "\n" +
                 STRING_CMD_MODIFY_USAGE2;
  _read_only   = false;
  _displays_id = false;
}

////////////////////////////////////////////////////////////////////////////////
int CmdModify::execute (std::string& output)
{
  int count = 0;
  std::stringstream out;

  // Apply filter.
  std::vector <Task> filtered;
  filter (filtered);
  if (filtered.size () == 0)
  {
    context.footnote (STRING_FEEDBACK_NO_TASKS_SP);
    return 1;
  }

  // Apply the command line modifications to the new task.
  A3 modifications = context.a3.extract_modifications ();
  if (!modifications.size ())
    throw std::string (STRING_CMD_XPEND_NEED_TEXT);

  Permission permission;
  if (filtered.size () > (size_t) context.config.getInteger ("bulk"))
    permission.bigSequence ();

  std::vector <Task>::iterator task;
  for (task = filtered.begin (); task != filtered.end (); ++task)
  {
    Task before (*task);
    modify_task_description_replace (*task, modifications);

    // Perform some logical consistency checks.
    if (task->has ("recur")  &&
        !task->has ("due")   &&
        !before.has ("due"))
      throw std::string (STRING_CMD_MODIFY_NO_DUE);

    if (task->has ("until")  &&
        !task->has ("recur") &&
        !before.has ("recur"))
      throw std::string (STRING_CMD_MODIFY_UNTIL);

    if (before.has ("recur") &&
        before.has ("due")   &&
        (!task->has ("due")  ||
         task->get ("due") == ""))
      throw std::string (STRING_CMD_MODIFY_REM_DUE);

    if (before.has ("recur")  &&
        task->has ("recur")   &&
        (!task->has ("recur") ||
         task->get ("recur") == ""))
      throw std::string (STRING_CMD_MODIFY_REC_ALWAYS);

    if (taskDiff (before, *task) &&
        permission.confirmed (*task, taskDifferences (before, *task) + "Proceed with change?"))
    {
      // Checks passed, modify the task.
      ++count;
      context.tdb2.modify (*task);
      if (before.get ("project") != task->get ("project"))
        context.footnote (onProjectChange (before, *task));

      // Make all changes.
      bool warned = false;
      std::vector <Task> siblings = context.tdb2.siblings (*task);
      std::vector <Task>::iterator sibling;
      for (sibling = siblings.begin (); sibling != siblings.end (); ++sibling)
      {
        if (before.has ("parent") && !warned)
        {
          warned = true;
          std::cout << format (STRING_CMD_MODIFY_INSTANCES, before.id) << "\n";
        }

        Task alternate (*sibling);

        // If a task is being made recurring, there are other cascading
        // changes.
        if (!before.has ("recur") &&
            task->has ("recur"))
        {
          sibling->setStatus (Task::recurring);
          sibling->set ("mask", "");

          std::cout << format (STRING_CMD_MODIFY_NOW_RECUR, sibling->id) << "\n";
        }

        // Apply other deltas.
        modify_task_description_replace (*sibling, modifications);

        if (taskDiff (alternate, *sibling))
        {
          if (permission.confirmed (alternate,
                                    taskDifferences (alternate, *sibling) +
                                                     STRING_CMD_MODIFY_PROCEED))
          {
            // TODO Are dependencies being explicitly removed?
            //      Either we scan context.task for negative IDs "depends:-n"
            //      or we ask deltaAttributes (above) to record dependency
            //      removal.
            dependencyChainOnModify (alternate, *sibling);
            context.tdb2.modify (*sibling);
            ++count;

            if (alternate.get ("project") != sibling->get ("project"))
              context.footnote (onProjectChange (alternate, *sibling));

          }
        }
      }
    }
  }

  context.tdb2.commit ();

  if (context.verbose ("affected") ||
      context.config.getBoolean ("echo.command")) // Deprecated 2.0
  {
    if (count == 1)
      out << format (STRING_CMD_MODIFY_TASK, count) << "\n";
    else
      out << format (STRING_CMD_MODIFY_TASKS, count) << "\n";
  }

  output = out.str ();
  return 0;
}

////////////////////////////////////////////////////////////////////////////////
