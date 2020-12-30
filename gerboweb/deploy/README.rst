Admin foo howto
===============

Ansible
-------

Selectively run ansible playbooks for the git service and webserver setup:

.. code-block::

   ansible-playbook -i inventory.yml -t git,www playbook.yml

Gitolite/CGIT
-------------

Remove ad-hoc repo from command line:

.. code-block::

   ssh git@git.jaseg.de unlock sjandrakei/pub/usb-remote
   ssh git@git.jaseg.de D unlock sjandrakei/pub/usb-remote

Set ad-hoc repo description from command line:

.. code-block::

   ssh git@git.jaseg.de desc sjandrakei/pub/kochbuch Bringing analog recipe books into the interwebs

Create ad-hoc repo from command line:

.. code-block::
   
   git clone git@git.jaseg.de:sjandrakei/pub/repo-to-be-created.git
